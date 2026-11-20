// SPDX-License-Identifier: GPL-2.0
/*
 * The memory side of fork().
 *
 * A forked child must find a copy of its parent's memory at the same
 * addresses, and there is no MMU to put other pages there. What makes it
 * possible anyway is that only one task runs at a time (one CPU, and user
 * programs run on it too): two processes never look at the same address
 * at the same moment. So parent and child share the pages (the nommu
 * regions, see nommu_dup_vmas() in mm/nommu.c), and the kernel keeps one
 * copy of the contents per process, exchanging them when it switches to
 * a process whose contents are not the ones in place:
 *
 *   wasm_fork_region   a private region that more than one address space
 *                      maps since a fork()
 *   wasm_fork_member   one of these address spaces. Exactly one is the
 *                      "resident", whose contents are in the region; every
 *                      other one has a "shadow" buffer with its contents.
 *
 * fork() gives the child a shadow with a copy of the region (the copying a
 * fork does), switch_mm() exchanges region and shadow, and the shadow
 * becomes that of the address space that was resident. A region stops
 * being special when all but one address space have unmapped it, which
 * exec and exit do.
 *
 * The invariant everything relies on: the address space of the running
 * task is resident in all its regions. Kernel code that is not switching
 * tasks can therefore keep using user addresses directly.
 *
 * FIXME(wasm): a switch between related processes costs a copy of all
 * memory they still have in common since the fork, in each direction.
 * Access to the memory of a process that is not running
 * (access_process_vm(): /proc/PID/cmdline, environ) reads the resident's
 * contents. A region shared this way cannot be partially unmapped. And
 * none of this works with more than one CPU.
 *
 * Shared mappings (MAP_SHARED) are shared and need none of this.
 */
#include <linux/gfp.h>
#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/mmu_context.h>

struct wasm_fork_member;

struct wasm_fork_region {
	struct hlist_node hash;			/* wasm_fork_regions */
	struct vm_region *region;
	unsigned long start;
	size_t size;
	struct list_head members;
	/* NULL after the resident has unmapped the region. */
	struct wasm_fork_member *resident;
};

struct wasm_fork_member {
	struct list_head mm_node;		/* mm->context.fork_members */
	struct list_head region_node;		/* wasm_fork_region.members */
	struct wasm_fork_region *fr;
	void *shadow;				/* NULL for the resident */
};

/*
 * Keyed by region. No lock: one CPU and no preemption, and nothing below
 * sleeps between looking at these structures and being done with them.
 */
static DEFINE_HASHTABLE(wasm_fork_regions, 8);

static struct wasm_fork_region *wasm_fork_region_find(struct vm_region *region)
{
	struct wasm_fork_region *fr;

	hash_for_each_possible(wasm_fork_regions, fr, hash, (unsigned long)region)
		if (fr->region == region)
			return fr;
	return NULL;
}

static struct wasm_fork_member *wasm_fork_member_find(struct mm_struct *mm,
						      struct vm_region *region)
{
	struct wasm_fork_member *m;

	list_for_each_entry(m, &mm->context.fork_members, mm_node)
		if (m->fr->region == region)
			return m;
	return NULL;
}

static void wasm_fork_member_free(struct wasm_fork_member *m)
{
	list_del(&m->mm_node);
	list_del(&m->region_node);
	if (m->shadow)
		free_pages_exact(m->shadow, m->fr->size);
	kfree(m);
}

/*
 * Called when a member is gone or has become resident: a region with
 * nobody left, or with only its resident, is an ordinary region again.
 */
static void wasm_fork_region_check(struct wasm_fork_region *fr)
{
	if (!list_empty(&fr->members)) {
		if (!list_is_singular(&fr->members) || !fr->resident)
			return;
		wasm_fork_member_free(fr->resident);
	}
	hash_del(&fr->hash);
	kfree(fr);
}

static void wasm_fork_exchange(void *a, void *b, size_t size)
{
	static u8 tmp[PAGE_SIZE];

	while (size) {
		size_t n = min(size, sizeof(tmp));

		memcpy(tmp, a, n);
		memcpy(a, b, n);
		memcpy(b, tmp, n);
		a += n;
		b += n;
		size -= n;
	}
}

/* Called by the scheduler (switch_mm()) before a task of @next runs. */
void wasm_fork_switch_mm(struct mm_struct *next)
{
	struct wasm_fork_member *m, *tmp;

	list_for_each_entry_safe(m, tmp, &next->context.fork_members, mm_node) {
		struct wasm_fork_region *fr = m->fr;
		struct wasm_fork_member *old = fr->resident;

		if (old == m)
			continue;

		if (old) {
			wasm_fork_exchange((void *)fr->start, m->shadow, fr->size);
			old->shadow = m->shadow;
		} else {
			memcpy((void *)fr->start, m->shadow, fr->size);
			free_pages_exact(m->shadow, fr->size);
		}
		m->shadow = NULL;
		fr->resident = m;
		wasm_fork_region_check(fr);
	}
}

/* @mm no longer maps @vma (munmap, exec, exit). */
void arch_nommu_delete_vma(struct mm_struct *mm, struct vm_area_struct *vma)
{
	struct wasm_fork_member *m;
	struct wasm_fork_region *fr;

	if (!mm || list_empty(&mm->context.fork_members))
		return;
	m = wasm_fork_member_find(mm, vma->vm_region);
	if (!m)
		return;

	fr = m->fr;
	if (fr->resident == m)
		fr->resident = NULL;
	wasm_fork_member_free(m);
	wasm_fork_region_check(fr);
}

void wasm_fork_destroy_mm(struct mm_struct *mm)
{
	struct wasm_fork_member *m, *tmp;

	/* exit_mmap() has unmapped everything, so nothing should be left. */
	list_for_each_entry_safe(m, tmp, &mm->context.fork_members, mm_node) {
		struct wasm_fork_region *fr = m->fr;

		WARN_ON_ONCE(1);
		if (fr->resident == m)
			fr->resident = NULL;
		wasm_fork_member_free(m);
		wasm_fork_region_check(fr);
	}
}

static bool wasm_fork_vma_is_private(struct vm_area_struct *vma)
{
	return (vma->vm_region->vm_flags & VM_MAPPED_COPY) &&
	       !(vma->vm_flags & VM_MAYSHARE);
}

/*
 * fork(): @mm has just been given @oldmm's mappings, sharing the regions.
 * Give it its own copy of the contents of every private one. Called in the
 * parent, whose contents are therefore the ones in memory.
 */
int arch_nommu_dup_mmap(struct mm_struct *mm, struct mm_struct *oldmm)
{
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *vma;

	if (WARN_ON_ONCE(current->mm != oldmm))
		return -EINVAL;

	for_each_vma(vmi, vma) {
		struct vm_region *region = vma->vm_region;
		size_t size = region->vm_top - region->vm_start;
		struct wasm_fork_region *fr, *new_fr;
		struct wasm_fork_member *parent, *child;
		void *shadow;

		if (!wasm_fork_vma_is_private(vma))
			continue;

		/* Everything that can sleep first. */
		new_fr = kzalloc(sizeof(*new_fr), GFP_KERNEL);
		parent = kzalloc(sizeof(*parent), GFP_KERNEL);
		child = kzalloc(sizeof(*child), GFP_KERNEL);
		shadow = alloc_pages_exact(size, GFP_KERNEL);
		if (!new_fr || !parent || !child || !shadow) {
			kfree(new_fr);
			kfree(parent);
			kfree(child);
			if (shadow)
				free_pages_exact(shadow, size);
			return -ENOMEM;
		}

		fr = wasm_fork_region_find(region);
		if (fr) {
			/* The parent is the child of an earlier fork. */
			kfree(new_fr);
			kfree(parent);
			WARN_ON_ONCE(!fr->resident ||
				     fr->resident != wasm_fork_member_find(oldmm, region));
		} else {
			fr = new_fr;
			fr->region = region;
			fr->start = region->vm_start;
			fr->size = size;
			INIT_LIST_HEAD(&fr->members);
			hash_add(wasm_fork_regions, &fr->hash, (unsigned long)region);

			parent->fr = fr;
			list_add(&parent->mm_node, &oldmm->context.fork_members);
			list_add(&parent->region_node, &fr->members);
			fr->resident = parent;
		}

		memcpy(shadow, (void *)fr->start, size);
		child->fr = fr;
		child->shadow = shadow;
		list_add(&child->mm_node, &mm->context.fork_members);
		list_add(&child->region_node, &fr->members);
	}
	return 0;
}
