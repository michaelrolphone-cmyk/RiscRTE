extern void bound_files_app(void);
extern void bound_files_app_fini(void);
__attribute__((visibility("default"))) int app_module_init(void){return 0;}
__attribute__((visibility("default"))) void app_module_fini(void){bound_files_app_fini();}
__attribute__((visibility("default"))) void app_main(void){bound_files_app();}
