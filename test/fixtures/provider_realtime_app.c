extern void test_provider_app(void);
extern void test_provider_lifecycle(void);
__attribute__((visibility("default"))) int app_module_init(void){test_provider_lifecycle();return 0;}
__attribute__((visibility("default"))) void app_module_fini(void){test_provider_lifecycle();}
__attribute__((visibility("default"))) void app_main(void){test_provider_app();}
