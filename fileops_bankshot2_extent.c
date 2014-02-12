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
	DEBUG("0x%.16llx to 0x%.16llx %d\n", current->offset,
		current->offset + current->count, current->dirty);
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
int find_extent(struct NVFile *nvf, off_t *offset, size_t *count)
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
	if (current->offset + current->count >= *offset + *count) {
		return 1;
	} else { // Partially covered
		*count -= current->offset + current->count - *offset;
		*offset = current->offset + current->count;
		return 2;
	}
}

/* Add an extent to cache tree */
/* Must hold Write lock of NVNode */
void add_extent(struct NVFile *nvf, off_t offset, size_t count, int write)
{
	struct NVNode *node = nvf->node;
	struct extent_cache_entry *new;
	rb_red_blk_tree *tree = node->extent_tree;
	rb_red_blk_node *newnode, *prenode;

	new = malloc(sizeof(struct extent_cache_entry));
	if (!new)
		assert(0);

	new->offset = offset; 
	new->count = count;
	new->dirty = write; 
	new->next = NULL;
	new->dirty = write;

	newnode = RBTreeInsert(tree, new, NULL);
	prenode = TreePredecessor(tree, newnode);
	if (prenode != tree->nil) {
		struct extent_cache_entry *prev = prenode->key;
		if (prev->offset + prev->count >= new->offset) {
			if (prev->offset + prev->count
					< new->offset + new->count) {
				prev->count = new->offset + new->count
						- prev->offset;
			}

			free(newnode->key);
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
			if (new->offset + new->count >= next->offset) {
				if (next->offset + next->count
						> new->offset + new->count) {
					new->count = next->offset + next->count
							- new->offset;
				}

				if (next->dirty)
					new->dirty = next->dirty;
				free(sucnode->key);
				RBDelete(tree, sucnode);
			} else {
				break;
			}
		} else {
			break;
		}
	}
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

	free(x->key);
	RBDelete(tree, x);
	return;
}
