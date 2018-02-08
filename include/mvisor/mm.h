#ifndef _MVISOR_MM_H_
#define _MVISOR_MM_H_

#include <mvisor/list.h>

int vmm_mm_init(void);
char *vmm_malloc(size_t size);
char *vmm_alloc_pages(int pages);

struct mm_struct {
	void *page_table_base;
	struct list_head mem_list;
};

static inline char *vmm_alloc_page(void)
{
	return vmm_alloc_pages(1);
}

#endif