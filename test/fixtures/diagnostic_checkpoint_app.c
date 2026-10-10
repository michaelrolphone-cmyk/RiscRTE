extern int checkpoint_test_stage(unsigned);
__attribute__((visibility("default"))) int app_module_init(void){return checkpoint_test_stage(0);}
__attribute__((visibility("default"))) void app_main(void){(void)checkpoint_test_stage(1);}
__attribute__((visibility("default"))) void app_module_fini(void){(void)checkpoint_test_stage(2);}
