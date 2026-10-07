extern void demand_app(void);
extern void demand_fini(void);
__attribute__((visibility("default"))) int app_module_init(void){return 0;}
__attribute__((visibility("default"))) void app_main(void){demand_app();}
__attribute__((visibility("default"))) void app_module_fini(void){demand_fini();}
