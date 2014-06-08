#include "fileops_bankshot2.h"

int extent_rbtree_compare(struct extent_cache_entry *curr,
		struct extent_cache_entry *new)
{
	if (new->offset < curr->offset) return -1;
	if (new->offset > curr->offset) return 1;

	return 0;
}

int extent_rbtree_compare_find(struct extent_cache_entry *curr,
		off_t offset)
{
	if ((curr->offset <= offset) &&
			(curr->offset + curr->count > offset))
		return 0;

	if (offset < curr->offset) return -1;
	if (offset > curr->offset) return 1;

	return 0;
}

int mmap_rbtree_compare_find(struct extent_cache_entry *curr,
		unsigned long mmap_addr)
{
	if ((curr->mmap_addr <= mmap_addr) &&
			(curr->mmap_addr + curr->count > mmap_addr))
		return 0;

	if (mmap_addr < curr->mmap_addr) return -1;
	if (mmap_addr > curr->mmap_addr) return 1;

	return 0;
}

void extent_rbtree_printkey(struct extent_cache_entry *current)
{
	MSG("0x%.16llx to 0x%.16llx %d, mmap addr %lx\n", current->offset,
		current->offset + current->count, current->dirty,
		current->mmap_addr);
}

void bankshot2_print_extent_tree(struct NVNode *node)
{
	struct extent_cache_entry *curr;
	struct rb_node *temp;

	temp = rb_first(&node->extent_tree);
	MSG("Cache fd %d has %d extents\n", node->cache_fd, node->num_extents);
	while (temp) {
		curr = container_of(temp, struct extent_cache_entry, node);
		extent_rbtree_printkey(curr);
		temp = rb_next(temp);
	}

	return;
}

void bankshot2_cleanup_extent_tree(struct NVNode *node)
{
	struct extent_cache_entry *curr;
	struct rb_node *temp;

	temp = rb_first(&node->extent_tree);
	while (temp) {
		curr = container_of(temp, struct extent_cache_entry, node);
		temp = rb_next(temp);
		rb_erase(&curr->node, &node->extent_tree);
		rb_erase(&curr->mmap_node, &node->mmap_extent_tree);
		free(curr);
	}

	node->num_extents = 0;
	return;
}

/* Find an extent in cache tree */
/* Read lock of NVFile and NVNode must be held */
/* offset, count and mmap_addr must be aligned to PAGE_SIZE */
int find_extent(struct NVFile *nvf, off_t *offset, size_t *count,
			unsigned long *mmap_addr)
{
	struct NVNode *node = nvf->node;
	struct extent_cache_entry *curr;
	struct rb_node *temp;
	int compVal;

	temp = node->extent_tree.rb_node;

	while (temp) {
		curr = container_of(temp, struct extent_cache_entry, node);
		compVal = extent_rbtree_compare_find(curr, *offset);

		if (compVal == -1)
			temp = temp->rb_left;
		else if (compVal == 1)
			temp = temp->rb_right;
		else 
			goto found;
	}

	return 0;

found:
	*mmap_addr = curr->mmap_addr;
	*offset = curr->offset;
	*count = curr->count;
	return 1;
}

/* Add an extent to cache tree */
/* Must hold Write lock of NVNode */
/* offset, count and mmap_addr must be aligned to PAGE_SIZE */
void add_extent(struct NVFile *nvf, off_t offset, size_t length, int write,
			unsigned long mmap_addr)
{
	struct NVNode *node = nvf->node;
	struct extent_cache_entry *curr, *new, *next;
	struct rb_node **temp, *parent, *next_node;
	off_t extent_offset;
	size_t extent_length;
	unsigned long extent_mmap_addr;
	int compVal;
	int no_new = 0;

	/* Offset and length must be aligned to PAGE_SIZE */
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

	extent_offset = offset;
	extent_length = length;
	extent_mmap_addr = mmap_addr;

	temp = &(node->extent_tree.rb_node);
	parent = NULL;

	while (*temp) {
		curr = container_of(*temp, struct extent_cache_entry, node);
		compVal = extent_rbtree_compare_find(curr, extent_offset);

		parent = *temp;

		if (compVal == -1) {
			temp = &((*temp)->rb_left);
		} else if (compVal == 1) {
			temp = &((*temp)->rb_right);
		} else { 
		/* Found existing extent */
			if (extent_offset != curr->offset
					|| extent_length != curr->count
					|| extent_mmap_addr != curr->mmap_addr)
				ERROR("%s: Found unmatch existing extent: "
					"insert offset 0x%lx, length %lu, "
					"mmap_addr 0x%lx, existing offset "
					"0x%lx, length %lu, mmap_addr 0x%lx\n",
					__func__, extent_offset, extent_length,
					extent_mmap_addr, curr->offset,
					curr->count, curr->mmap_addr);
			no_new = 1;
			break;
		}
	}

	if (no_new)
		return;

	new = malloc(sizeof(struct extent_cache_entry));
	if (!new)
		assert(0);

	new->offset = extent_offset; 
	new->count = extent_length;
	new->dirty = write; 
	new->mmap_addr = extent_mmap_addr;

	rb_link_node(&new->node, parent, temp);
	rb_insert_color(&new->node, &node->extent_tree);

	next_node = rb_next(&new->node);
	if (!next_node)
		goto mmap_tree;

	next = container_of(next_node, struct extent_cache_entry, node);
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

mmap_tree:
	/*
	 * Now insert to the mmap rbtree.
	 * If find the existing mmap_addr but not the same offset,
	 * it means the old mapping needs to be removed, as two different
	 * offset cannot have the same mmap address.
	 */
	temp = &(node->mmap_extent_tree.rb_node);
	parent = NULL;

	while (*temp) {
		curr = container_of(*temp, struct extent_cache_entry,
					mmap_node);
		compVal = mmap_rbtree_compare_find(curr, extent_mmap_addr);

		if (compVal == -1) {
			temp = &((*temp)->rb_left);
		} else if (compVal == 1) {
			temp = &((*temp)->rb_right);
		} else { 
			/* Found existing extent */
			if (extent_offset != curr->offset) {
//					&& extent_mmap_addr == curr->mmap_addr) {
				DEBUG("Remove extent: start offset 0x%llx, "
					"mmap_addr 0x%llx, length %llu\n",
					curr->offset, curr->mmap_addr,
					curr->count);
				rb_erase(&curr->node, &node->extent_tree);
				rb_erase(&curr->mmap_node,
						&node->mmap_extent_tree);
				free(curr);
				node->num_extents--;
			}
			break;
		}
	}

	temp = &(node->mmap_extent_tree.rb_node);
	parent = NULL;

	while (*temp) {
		curr = container_of(*temp, struct extent_cache_entry,
					mmap_node);
		compVal = mmap_rbtree_compare_find(curr, extent_mmap_addr);

		parent = *temp;

		if (compVal == -1) {
			temp = &((*temp)->rb_left);
		} else if (compVal == 1) {
			temp = &((*temp)->rb_right);
		} else { 
			ERROR("%s: Existing extent with same mmap address: "
				"insert offset 0x%lx, length %lu, "
				"mmap_addr 0x%lx, existing offset "
				"0x%lx, length %lu, mmap_addr 0x%lx\n",
				__func__, extent_offset, extent_length,
				extent_mmap_addr, curr->offset,
				curr->count, curr->mmap_addr);
//			assert(0);
		}
	}

	rb_link_node(&new->mmap_node, parent, temp);
	rb_insert_color(&new->mmap_node, &node->mmap_extent_tree);

	node->num_extents++;
	return;

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
	struct extent_cache_entry *curr;
	struct rb_node *temp;
	int compVal;

	temp = node->extent_tree.rb_node;
	while (temp) {
		curr = container_of(temp, struct extent_cache_entry, node);
		compVal = extent_rbtree_compare_find(curr, offset);

		if (compVal == -1) {
			temp = temp->rb_left;
		} else if (compVal == 1) {
			temp = temp->rb_right;
		} else {
			rb_erase(&curr->node, &node->extent_tree);
			rb_erase(&curr->mmap_node, &node->mmap_extent_tree);
			free(curr);
			node->num_extents--;
			break;
		}
	}

	return;
}

/* Find the first extent in cache tree */
/* Read lock of NVFile and NVNode must be held */
int first_extent(struct NVFile *nvf, off_t *offset, size_t *count, int *dirty,
				unsigned long *mmap_addr)
{
	struct NVNode *node = nvf->node;
	struct extent_cache_entry *curr;
	struct rb_node *temp;
//	int compVal;

	temp = rb_first(&node->extent_tree);
	if (!temp)
		return 0;

	curr = container_of(temp, struct extent_cache_entry, node);

	*count = curr->count;
	*offset = curr->offset;
	*dirty = curr->dirty;
	*mmap_addr = curr->mmap_addr;

	return 1;
}
