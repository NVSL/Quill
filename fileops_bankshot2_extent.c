#include "fileops_bankshot2.h"
#include "red_black_tree.h"

int extent_rbtree_compare(const void *left, const void *right)
{
	const struct extent_cache_entry *leftkey = left;
	const struct extent_cache_entry *rightkey = right;

	if (leftkey->offset < rightkey->offset) return -1;
	if (leftkey->offset > rightkey->offset) return 1;

	return 0;
}

int extent_rbtree_compare_find(const void *node, const off_t offset)
{
	const struct extent_cache_entry *nodekey = node;

	if ((nodekey->offset <= offset) &&
			(nodekey->offset + nodekey->count > offset))
		return 0;

	if (nodekey->offset < offset) return -1;
	if (nodekey->offset > offset) return 1;

	return 0;
}

void extent_rbtree_destroykey(void *key)
{
	free(key);
}

void extent_rbtree_destroyinfo(void *key)
{
}

void extent_rbtree_printkey(const void *key)
{
	const struct extent_cache_entry *current = key;
	MSG("0x%.16llx to 0x%.16llx %d, mmap addr %lx\n", current->offset,
		current->offset + current->count, current->dirty,
		current->mmap_addr);
}

void extent_rbtree_printinfo(void *key)
{
}

void bankshot2_setup_extent_tree(struct NVNode *node)
{
	node->extent_tree = RBTreeCreate(
		extent_rbtree_compare,
		extent_rbtree_destroykey,
		extent_rbtree_destroyinfo,
		extent_rbtree_printkey,
		extent_rbtree_printinfo
	);
}

void bankshot2_cleanup_extent_tree(struct NVNode *node)
{
	RBTreeDestroy((rb_red_blk_tree *)(node->extent_tree));
}

/* Find an extent in cache tree */
/* Read lock of NVFile and NVNode must be held */
/* offset, count and mmap_addr must be aligned to PAGE_SIZE */
int find_extent(struct NVFile *nvf, off_t *offset, size_t *count,
			unsigned long *mmap_addr)
{
	struct NVNode *node = nvf->node;
	rb_red_blk_node *x;
	rb_red_blk_node *nil;
	rb_red_blk_tree *tree = node->extent_tree;
	int compVal;

	x = tree->root->left;
	nil = tree->nil;

	if (x == nil)
		return 0;

	compVal = extent_rbtree_compare_find(x->key, *offset);
	while (compVal) {
		if (compVal == 1)
			x = x->left;
		else
			x = x->right;

		if (x == nil)
			return 0;

		compVal = extent_rbtree_compare_find(x->key, *offset);
	}

	// found a matching node, return relevant values
	struct extent_cache_entry *current = x->key;

	// Fully covered
//	if (current->offset + current->count >= *offset + *count) {
	*mmap_addr = current->mmap_addr;
	*offset = current->offset;
	*count = current->count;
	return 1;
//	} else { // Partially covered
//		*count -= current->offset + current->count - *offset;
//		*offset = current->offset + current->count;
//		*mmap_addr = current->mmap_addr;
//		*offset = current->offset;
//		*count = current->count;
//		return 2;
//	}
}

/* Add an extent to cache tree */
/* Must hold Write lock of NVNode */
/* offset, count and mmap_addr must be aligned to PAGE_SIZE */
void add_extent(struct NVFile *nvf, off_t offset, size_t length, int write,
			unsigned long mmap_addr)
{
	struct NVNode *node = nvf->node;
	struct extent_cache_entry *new;
	const struct extent_cache_entry *nodekey;
	rb_red_blk_tree *tree = node->extent_tree;
	rb_red_blk_node *newnode, *sucnode;
	rb_red_blk_node *x;
	rb_red_blk_node *nil;
	off_t extent_offset;
	size_t extent_length;
	unsigned long extent_mmap_addr;
	int compVal;

	/* Break the extent to 2MB chunks */
	if (offset != ALIGN_DOWN(offset) || length != ALIGN_DOWN(length)) {
		ERROR("%s: offset or length not aligned to mmap unit size! "
			"offset 0x%lx, length %lu\n", offset, length);
		assert(0);
	}

	if (length > MAX_MMAP_SIZE) {
		ERROR("%s: length larger than 2MB! length %lu\n", length);
		assert(0);
	}

	DEBUG("Add extent offset 0x%lx, length %lu\n", offset, length);
	nil = tree->nil;

	x = tree->root->left;

	extent_offset = offset;
	extent_length = length;
	extent_mmap_addr = mmap_addr;

	if (x == nil)
		goto insert;

	compVal = extent_rbtree_compare_find(x->key, extent_offset);
	while (compVal) {
		if (compVal == 1)
			x = x->left;
		else
			x = x->right;

		if (x == nil)
			goto insert;

		compVal = extent_rbtree_compare_find(x->key,
				extent_offset);
	}

		/* Found existing extent */
	nodekey = (const struct extent_cache_entry *)(x->key);
	ERROR("%s: Found existing extent: "
		"insert offset 0x%lx, length %lu, mmap_addr 0x%lx, "
		"existing offset 0x%lx, length %lu, mmap_addr 0x%lx\n",
		__func__, extent_offset, extent_length,
		extent_mmap_addr, nodekey->offset, nodekey->count,
		nodekey->mmap_addr);
	return;

insert:
	new = malloc(sizeof(struct extent_cache_entry));
	if (!new)
		assert(0);

	new->offset = extent_offset; 
	new->count = extent_length;
	new->dirty = write; 
	new->next = NULL;
	new->dirty = write;
	new->mmap_addr = extent_mmap_addr;

	newnode = RBTreeInsert(tree, new, NULL);
	if (!newnode)
		assert(0);

	sucnode = TreeSuccessor(tree, newnode);
	if (sucnode != tree->nil) {
		struct extent_cache_entry *next = sucnode->key;
		if (new->offset + new->count > next->offset) {
			ERROR("%s: insert extent overlaps "
				"with existing extent: insert offset 0x%lx, "
				"length %lu, mmap_addr 0x%lx, existing offset "
				"0x%lx, length %lu, mmap_addr 0x%lx\n",
				__func__, extent_offset, extent_length,
				extent_mmap_addr, next->offset, next->count,
				next->mmap_addr);
			new->count = next->offset - new->offset;
		}
	}
#if 0
	prenode = TreePredecessor(tree, newnode);
	if (prenode != tree->nil) {
		struct extent_cache_entry *prev = prenode->key;
		if ((prev->offset + prev->count >= new->offset) &&
		    (prev->mmap_addr + (new->offset - prev->offset) == new->mmap_addr)) {
			if (prev->offset + prev->count
					< new->offset + new->count) {
				prev->count = new->offset + new->count
						- prev->offset;
			}

			RBDelete(tree, newnode);

			newnode = prenode;
			new = newnode->key;
			if (write)
				new->dirty = write;
		}
	}

	/* Loop because the new extent might cover several existing extents */
	while (1) {
		rb_red_blk_node *sucnode = TreeSuccessor(tree, newnode);
		if (sucnode != tree->nil) {
			struct extent_cache_entry *next = sucnode->key;
			if ((new->offset + new->count >= next->offset) &&
		 	    (new->mmap_addr + (next->offset - new->offset) == next->mmap_addr)) {
				if (next->offset + next->count
						> new->offset + new->count) {
					new->count = next->offset + next->count
							- new->offset;
				}

				if (next->dirty)
					new->dirty = next->dirty;
				RBDelete(tree, sucnode);
			} else {
				break;
			}
		} else {
			break;
		}
	}
#endif
}

void remove_extent(struct NVFile *nvf, off_t offset)
{
	struct NVNode *node = nvf->node;
	int compVal;
	rb_red_blk_tree *tree = node->extent_tree;
	rb_red_blk_node *x;
	rb_red_blk_node *nil;

	x = tree->root->left;
	nil = tree->nil;

	if (x == nil)
		return;

	compVal = extent_rbtree_compare_find(x->key, offset);
	while (compVal) {
		if (compVal == 1)
			x = x->left;
		else
			x = x->right;

		if (x == nil)
			return;

		compVal = extent_rbtree_compare_find(x->key, offset);
	}

	DEBUG("Remove extent offset 0x%lx\n", offset);
	RBDelete(tree, x);
	return;
}

/* Find the first extent in cache tree */
/* Read lock of NVFile and NVNode must be held */
int first_extent(struct NVFile *nvf, off_t *offset, size_t *count, int *dirty,
				unsigned long *mmap_addr)
{
	struct NVNode *node = nvf->node;
	rb_red_blk_node *x;
	rb_red_blk_node *nil;
	rb_red_blk_tree *tree = node->extent_tree;
//	int compVal;

	x = tree->root->left;
	nil = tree->nil;

	if (x == nil)
		return 0;

	while (x->left != nil)
		x = x->left;

	// found a matching node, return relevant values
	struct extent_cache_entry *current = x->key;

	*count = current->count;
	*offset = current->offset;
	*dirty = current->dirty;
	*mmap_addr = current->mmap_addr;

	return 1;
}
