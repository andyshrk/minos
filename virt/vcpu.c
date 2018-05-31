#include <minos/minos.h>
#include <minos/sched.h>
#include <config/config.h>
#include <minos/mm.h>
#include <minos/bitmap.h>
#include <minos/os.h>
#include <minos/task.h>
#include <virt/virt.h>
#include <virt/vm.h>
#include <virt/vcpu.h>
#include <virt/vmodule.h>
#include <virt/virq.h>

extern unsigned char __vm_start;
extern unsigned char __vm_end;

static struct vm *vms[CONFIG_MAX_VM];
static uint32_t total_vms = 0;

LIST_HEAD(vm_list);

static int add_vm(struct vmtag *vme)
{
	struct vm *vm;

	if (!vme)
		return -EINVAL;

	vm = (struct vm *)malloc(sizeof(struct vm));
	if (!vm)
		return -ENOMEM;

	memset((char *)vm, 0, sizeof(struct vm));
	vm->vmid = vme->vmid;
	strncpy(vm->name, vme->name,
		MIN(strlen(vme->name), MINOS_VM_NAME_SIZE - 1));
	strncpy(vm->os_type, vme->type,
		MIN(strlen(vme->type), OS_TYPE_SIZE - 1));
	vm->vcpu_nr = MIN(vme->nr_vcpu, CONFIG_VM_MAX_VCPU);
	vm->mmu_on = vme->mmu_on;
	vm->entry_point = vme->entry;
	vm->setup_data = vme->setup_data;
	memcpy(vm->vcpu_affinity, vme->vcpu_affinity,
			sizeof(uint32_t) * CONFIG_VM_MAX_VCPU);

	vm->index = total_vms;
	vms[total_vms] = vm;
	total_vms++;
	list_add_tail(&vm_list, &vm->vm_list);

	vmodules_create_vm(vm);

	return 0;
}

static int parse_all_vms(void)
{
	int i;
	struct vmtag *vmtags = mv_config->vmtags;

	if (mv_config->nr_vmtag == 0) {
		pr_error("No VM is found\n");
		return -ENOENT;
	}

	pr_info("Found %d VMs config\n", mv_config->nr_vmtag);

	for (i = 0; i < mv_config->nr_vmtag; i++)
		add_vm(&vmtags[i]);

	return 0;
}

struct vm *get_vm_by_id(uint32_t vmid)
{
	int i;
	struct vm *vm;

	for (i = 0; i < total_vms; i++) {
		vm = vms[i];
		if (vm->vmid == vmid)
			return vm;
	}

	return NULL;
}

struct vcpu *get_vcpu_in_vm(struct vm *vm, uint32_t vcpu_id)
{
	if (vcpu_id >= vm->vcpu_nr)
		return NULL;

	return vm->vcpus[vcpu_id];
}

struct vcpu *get_vcpu_by_id(uint32_t vmid, uint32_t vcpu_id)
{
	struct vm *vm;

	vm = get_vm_by_id(vmid);
	if (!vm)
		return NULL;

	return get_vcpu_in_vm(vm, vcpu_id);
}

struct task *create_vcpu_task(struct vm *vm, uint32_t vcpu_id)
{
	struct vcpu *vcpu;
	struct task *task;
	char name[64];
	unsigned long flags;

	vcpu = (struct vcpu *)malloc(sizeof(struct vcpu));
	if (!vcpu)
		return NULL;

	memset((char *)vcpu, 0, sizeof(struct vcpu));
	vcpu->vcpu_id = vcpu_id;
	vcpu->vm = vm;

	vm->vcpus[vcpu_id] = vcpu;
	vcpu_virq_struct_init(&vcpu->virq_struct);

	memset(name, 0, 64);
	sprintf(name, "%s-vcpu-%d", vm->name, vcpu_id);
	flags = 0 | TASK_FLAG_VCPU;
	task = create_task(name, (void *)vm->entry_point,
			VCPU_TASK_DEFAULT_STACK_SIZE,
			vm->task_pr, vm->vcpu_affinity[vcpu_id],
			(void *)vcpu, flags);
	if (!task) {
		free(vcpu);
		return NULL;
	}

	vcpu->task = task;

	return task;
}

static void inline create_vcpu_tasks(struct vm *vm)
{
	int i;

	for (i = 0; i < vm->vcpu_nr; i++)
		create_vcpu_task(vm, i);
}

static void inline vm_vmodules_init(struct vm *vm)
{
	int i;

	for (i = 0; i < vm->vcpu_nr; i++)
		vcpu_vmodules_init(vm->vcpus[i]);
}

int create_vms(void)
{
	int i;
	struct vm *vm;

	if (parse_all_vms()) {
		pr_info("No virtual machine found\n");
		return 0;
	}

	for_each_vm(vm) {
		/*
		 * - map the vm's memory
		 * - create the task for vm's each vcpu
		 * - init the vmodule state for each vcpu
		 * - prepare the vcpu for bootup
		 */
		vm_mm_init(vm);
		create_vcpu_tasks(vm);
		vm->os = get_vm_os(vm->os_type);
		vm_vmodules_init(vm);

		for (i = 0; i < vm->vcpu_nr; i++)
			vm->os->ops->vcpu_init(vm->vcpus[i]);
	}

	return 0;
}