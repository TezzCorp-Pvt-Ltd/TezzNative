/* Shared typed runtime/VM/import contract. Define TN_NPU_API before inclusion. */
TN_NPU_API(tn_npu_has_backend, long long, 0, (void), (), 0)
TN_NPU_API(tn_npu_init, long long, 0, (void), (), 0)
TN_NPU_API(tn_npu_shutdown, long long, 0, (void), (), 0)
TN_NPU_API(tn_npu_device_count, long long, 0, (void), (), 0)
TN_NPU_API(tn_npu_set_device, long long, 1, (long long), (tn_vm_integer(args[0])), 0)
TN_NPU_API(tn_npu_alloc, unsigned char*, 1, (long long), (tn_vm_integer(args[0])), 1)
TN_NPU_API(tn_npu_free, long long, 1, (unsigned char*), ((unsigned char*)(intptr_t)tn_vm_integer(args[0])), 0)
TN_NPU_API(tn_npu_memcpy_to_device, long long, 3, (unsigned char*, unsigned char*, long long),
  ((unsigned char*)(intptr_t)tn_vm_integer(args[0]), (unsigned char*)(intptr_t)tn_vm_integer(args[1]), tn_vm_integer(args[2])), 0)
TN_NPU_API(tn_npu_memcpy_to_host, long long, 3, (unsigned char*, unsigned char*, long long),
  ((unsigned char*)(intptr_t)tn_vm_integer(args[0]), (unsigned char*)(intptr_t)tn_vm_integer(args[1]), tn_vm_integer(args[2])), 0)
TN_NPU_API(tn_npu_memset, long long, 3, (unsigned char*, long long, long long),
  ((unsigned char*)(intptr_t)tn_vm_integer(args[0]), tn_vm_integer(args[1]), tn_vm_integer(args[2])), 0)
TN_NPU_API(tn_npu_launch_1d, long long, 3, (long long, unsigned char*, long long),
  (tn_vm_integer(args[0]), (unsigned char*)(intptr_t)tn_vm_integer(args[1]), tn_vm_integer(args[2])), 0)
TN_NPU_API(tn_npu_launch_dxil_1d, long long, 4, (unsigned char*, long long, unsigned char*, long long),
  ((unsigned char*)(intptr_t)tn_vm_integer(args[0]), tn_vm_integer(args[1]), (unsigned char*)(intptr_t)tn_vm_integer(args[2]), tn_vm_integer(args[3])), 0)
TN_NPU_API(tn_npu_backend_name, unsigned char*, 0, (void), (), 1)
TN_NPU_API(tn_npu_supports_dxil, long long, 0, (void), (), 0)
TN_NPU_API(tn_npu_model_load, long long, 1, (unsigned char*), ((unsigned char*)(intptr_t)tn_vm_integer(args[0])), 0)
TN_NPU_API(tn_npu_model_free, long long, 1, (long long), (tn_vm_integer(args[0])), 0)
TN_NPU_API(tn_npu_model_run_f64_1d, long long, 5, (long long, double*, long long, double*, long long),
  (tn_vm_integer(args[0]), (double*)(intptr_t)tn_vm_integer(args[1]), tn_vm_integer(args[2]),
   (double*)(intptr_t)tn_vm_integer(args[3]), tn_vm_integer(args[4])), 0)
