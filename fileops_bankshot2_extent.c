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
	MSG("0x%.16llx to 0x%.16llx %d, mmap addr %lx, length %llu\n",
		current->offset, current->offset + current->count,
		current->dirty,	current->mmap_addr, current->count);
}

void bankshot2_print_extent_tree(struct NVNode *node)
{
	struct extent_cache_entry *curr;
	struct rb_node *temp;

	temp = rb_first(&node->extent_tree);
	MSG("Cache fd %d has %d extents\n", node->cache_serialno, node->num_extents);
	while (temp) {
		curr = container_of(temp, struct extent_cache_entry, node);
		extent_rbtree_printkey(curr);
		temp = rb_next(temp);
	}

	MSG("Mmap tree:\n");
	temp = rb_first(&node->mmap_extent_tree);
	while (temp) {
		curr = container_of(temp, struct extent_cache_entry, mmap_node);
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

static void bankshot2_free_btree(unsigned long *root, unsigned int height)
{
	int i;

	if (height == 0)
		return;

	for (i = 0; i < 1024; i++) {
		if (root[i]) {
			bankshot2_free_btree((unsigned long *)root[i],
						height - 1);
			root[i] = 0;
		}
	}

	free(root);
	return;
}

void bankshot2_cleanup_extent_btree(struct NVNode *node)
{
	struct extent_cache_entry *curr;
	struct rb_node *temp;
	unsigned int height = node->height;
	unsigned long *root;
	int i;

	temp = rb_first(&node->mmap_extent_tree);
	while (temp) {
		curr = container_of(temp, struct extent_cache_entry, node);
		temp = rb_next(temp);
		rb_erase(&curr->mmap_node, &node->mmap_extent_tree);
		free(curr);
	}

	node->num_extents = 0;
	root = node->root;

	if (height) {
		bankshot2_free_btree(root, height);
	}

	node->height = 0;
	if (!node->root) {
		node->root = malloc(1024 * sizeof(unsigned long));
		for (i = 0; i < 1024; i++)
			node->root[i] = 0;
	}

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

static unsigned long calculate_capacity(unsigned int height)
{
	unsigned long capacity = MAX_MMAP_SIZE;

	while(height) {
		capacity *= 1024;
		height--;
	}

	return capacity;
}

int find_extent_btree(struct NVFile *nvf, off_t *offset, size_t *count,
			unsigned long *mmap_addr)
{
	struct NVNode *node = nvf->node;
	unsigned int height = node->height;
	unsigned long capacity = MAX_MMAP_SIZE;
	unsigned long start_addr;
	unsigned long *root = node->root;
	off_t start_offset = *offset;
	int index;

	do {
		capacity = calculate_capacity(height);
		index = start_offset / capacity;
		if (index >= 1024 || root[index] == 0)
			return 0;
		if (height)
			root = (unsigned long *)root[index];
		else
			start_addr = root[index];
		start_offset = start_offset % capacity;
	} while (height--);

	*mmap_addr = start_addr;
	*offset = ALIGN_DOWN(*offset);
	*count = MAX_MMAP_SIZE;

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

retry:
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
			if (extent_offset != curr->offset) {
				ERROR("Insert new offset not match: "
					"cache fd %llu, offset 0x%lx, "
					"length %lu, existing offset 0x%lx, "
					"length %lu\n", nvf->cache_serialno,
					extent_offset, extent_length,
					curr->offset, curr->count);
				return;
			}
			if (extent_length <= curr->count) {
				DEBUG("%s: Found match existing extent: "
					"offset 0x%lx, length %lu, "
					"mmap_addr 0x%lx, "
					"existing offset 0x%lx, length %lu, "
					"mmap addr 0x%lx\n",
					__func__, extent_offset, extent_length,
					extent_mmap_addr, curr->offset,
					curr->count, curr->mmap_addr);
				return;
			}
			/* We may need to extend the mapping */
			if (extent_mmap_addr == curr->mmap_addr) {
				curr->count = extent_length;
				new = curr;
				goto overlap_check;
			}
			/* Things are complicated: The new extent extends the
			 * length but with a different mmap address;
			 * Remove the original extent and insert the new one. */

			rb_erase(&curr->node, &node->extent_tree);
			rb_erase(&curr->mmap_node, &node->mmap_extent_tree);
			free(curr);
			node->num_extents--;
			goto retry;
		}
	}

	new = malloc(sizeof(struct extent_cache_entry));
	if (!new)
		assert(0);

	new->offset = extent_offset; 
	new->count = extent_length;
	new->dirty = write; 
	new->mmap_addr = extent_mmap_addr;

	rb_link_node(&new->node, parent, temp);
	rb_insert_color(&new->node, &node->extent_tree);

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
			bankshot2_print_extent_tree(node);
		}
	}

	rb_link_node(&new->mmap_node, parent, temp);
	rb_insert_color(&new->mmap_node, &node->mmap_extent_tree);

	node->num_extents++;

overlap_check:
	next_node = rb_next(&new->node);
	if (next_node) {
		next = container_of(next_node,
				struct extent_cache_entry, node);
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

	next_node = rb_next(&new->mmap_node);
	if (next_node) {
		next = container_of(next_node,
				struct extent_cache_entry, mmap_node);
		if (new->mmap_addr + new->count > next->mmap_addr) {
			ERROR("%s: insert extent mmap overlaps "
				"with existing extent: insert offset 0x%lx, "
				"length %lu, mmap_addr 0x%lx, existing offset "
				"0x%lx, length %lu, mmap_addr 0x%lx\n",
				__func__, extent_offset, extent_length,
				extent_mmap_addr, next->offset, next->count,
				next->mmap_addr);
			new->count = next->mmap_addr - new->mmap_addr;
		}
	}

	return;
}

static unsigned int calculate_new_height(off_t offset)
{
	unsigned int height = 0;
	off_t temp_offset = offset / ((unsigned long)1024 * MAX_MMAP_SIZE);

	while (temp_offset) {
		temp_offset /= 1024;
		height++;
	}

	return height;
}

void add_extent_btree(struct NVFile *nvf, off_t offset, size_t length,
		int write, unsigned long mmap_addr)
{
	struct NVNode *node = nvf->node;
	struct extent_cache_entry *curr, *new, *next;
	struct rb_node **temp, *parent, *next_node;
	off_t extent_offset;
	size_t extent_length;
	unsigned long extent_mmap_addr;
	unsigned int height = node->height;
	unsigned int new_height;
	unsigned long capacity = MAX_MMAP_SIZE;
	unsigned long *root = node->root;
	off_t start_offset = offset;
	int compVal, i, index;

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

	height = node->height;
	new_height = calculate_new_height(offset);

	if (height < new_height) {
		MSG("Increase height from %u to %u\n", height, new_height);

		while (height < new_height) {
			unsigned long old_root = (unsigned long)node->root;
			node->root = malloc(1024 * sizeof(unsigned long));
			for (i = 0; i < 1024; i++)
				node->root[i] = 0;
			node->root[0] = (unsigned long)old_root;
			height++;
		}

		node->height = new_height;
		height = new_height;
	}

	root = node->root;
	do {
		capacity = calculate_capacity(height);
		index = start_offset / capacity;
		if (height) {
			if (root[index] == 0) {
				root[index] = (unsigned long)malloc(1024 *
						sizeof(unsigned long));
				root = (unsigned long *)root[index];
				for (i = 0; i < 1024; i++)
					root[i] = 0;
			} else
				root = (unsigned long *)root[index];
		} else {
			if (root[index] == extent_mmap_addr)
				return;
			root[index] = extent_mmap_addr;
		}
		start_offset = start_offset % capacity;
	} while (height--);

	new = malloc(sizeof(struct extent_cache_entry));
	if (!new)
		assert(0);

	new->offset = extent_offset;
	new->count = extent_length;
	new->dirty = write;
	new->mmap_addr = extent_mmap_addr;

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
			bankshot2_print_extent_tree(node);
		}
	}

	rb_link_node(&new->mmap_node, parent, temp);
	rb_insert_color(&new->mmap_node, &node->mmap_extent_tree);

	node->num_extents++;

	next_node = rb_next(&new->mmap_node);
	if (next_node) {
		next = container_of(next_node,
				struct extent_cache_entry, mmap_node);
		if (new->mmap_addr + new->count > next->mmap_addr) {
			ERROR("%s: insert extent mmap overlaps "
				"with existing extent: insert offset 0x%lx, "
				"length %lu, mmap_addr 0x%lx, existing offset "
				"0x%lx, length %lu, mmap_addr 0x%lx\n",
				__func__, extent_offset, extent_length,
				extent_mmap_addr, next->offset, next->count,
				next->mmap_addr);
			new->count = next->mmap_addr - new->mmap_addr;
		}
	}

	return;
}

void remove_extent(struct NVFile *nvf, off_t offset)
{
	struct NVNode *node = nvf->node;
	struct extent_cache_entry *curr;
	struct rb_node *temp;
	int compVal;
	int removed = 0;

	MSG("Remove extent offset 0x%lx\n", offset);
	temp = node->extent_tree.rb_node;
	while (temp) {
		curr = container_of(temp, struct extent_cache_entry, node);
		compVal = extent_rbtree_compare_find(curr, offset);

		if (compVal == -1) {
			temp = temp->rb_left;
		} else if (compVal == 1) {
			temp = temp->rb_right;
		} else {
			MSG("Remove extent succeed: start offset 0x%llx, "
				"mmap_addr 0x%llx, length %llu\n",
				curr->offset, curr->mmap_addr,
				curr->count);
			rb_erase(&curr->node, &node->extent_tree);
			rb_erase(&curr->mmap_node, &node->mmap_extent_tree);
			free(curr);
			node->num_extents--;
			removed = 1;
			break;
		}
	}

	if (removed == 0)
		ERROR("Remove extent failed: offset 0x%llx not found\n",
				offset);
	return;
}

void remove_extent_btree(struct NVFile *nvf, off_t offset)
{
	struct NVNode *node = nvf->node;
	struct extent_cache_entry *curr;
	struct rb_node *temp;
	unsigned int height = node->height;
	unsigned long capacity = MAX_MMAP_SIZE;
	unsigned long *root = node->root;
	off_t start_offset = offset;
	int compVal, index;
	int removed = 0;

	MSG("Remove extent offset 0x%lx\n", offset);
	do {
		capacity = calculate_capacity(height);
		index = start_offset / capacity;
		if (index >= 1024 || root[index] == 0) {
			ERROR("Remove extent 0x%lx not found\n", offset);
			assert(0);
		}
		if (height)
			root = (unsigned long *)root[index];
		else
			root[index] = 0;
		start_offset = start_offset % capacity;
	} while (height--);

	temp = node->mmap_extent_tree.rb_node;
	while (temp) {
		curr = container_of(temp, struct extent_cache_entry, node);
		compVal = extent_rbtree_compare_find(curr, offset);

		if (compVal == -1) {
			temp = temp->rb_left;
		} else if (compVal == 1) {
			temp = temp->rb_right;
		} else {
			MSG("Remove extent succeed: start offset 0x%llx, "
				"mmap_addr 0x%llx, length %llu\n",
				curr->offset, curr->mmap_addr,
				curr->count);
			rb_erase(&curr->mmap_node, &node->mmap_extent_tree);
			free(curr);
			node->num_extents--;
			removed = 1;
			break;
		}
	}

	if (removed == 0)
		ERROR("Remove extent failed: offset 0x%llx not found\n",
				offset);
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
