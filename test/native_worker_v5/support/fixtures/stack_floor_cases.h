// Independent arithmetic witness for the pinned tasks.c and Xtensa port.c
// initializer, not execution of that port on the host. Actual pure floor C is
// linked separately. All simulated writes use signed wide offsets to expose
// underflow rather than wrapping an address or touching host memory.
static bool port_writes_fit(const esp_cpu_task_stack_layout_v1_t &l,uint32_t total,uint32_t working,unsigned residue){
    const int64_t base=0x10000+residue,end=base+total;
    auto down=[](int64_t p){return p&~int64_t(15);};
    int64_t sp=down(end-1); // tasks.c initial top, before pxPortInitialiseStack
    bool valid=sp>=base&&sp<end;
    auto write=[&](int64_t start,int64_t count){valid=valid&&count>=0&&start>=base&&start+count<=end;};
    if(l.coprocessor_bytes){
        sp=down(sp-l.coprocessor_bytes);
        write(sp,12); // actual CPSA p[0],p[1],p[2] stores
        write(sp,l.coprocessor_bytes); // entire reserved save region
    }
    int64_t data=l.tls_data_end-l.tls_data_start,bss=l.tls_bss_end-l.tls_bss_start;
    int64_t tls=((data+bss+15)/16)*16;
    sp=down(sp-tls);write(sp,data);write(sp+data,bss);
    int64_t previous=sp;sp=down(sp-l.initial_frame_bytes);
    write(sp,previous-sp); // actual frame memset; frame fields are inside it
    write(sp-working,working); // caller's separate downward working region
    return valid;
}
static void run_floor_case(const std::string &name){
    auto l=current_layout;uint32_t output=0x76543210,working=1024;
    if(name=="floor-null"){
        CHECK(!esp_cpu_task_stack_floor_v1(nullptr,working,&output));CHECK(output==0x76543210);
        CHECK(!esp_cpu_task_stack_floor_v1(&l,working,nullptr));return;
    }
    if(name=="floor-valid-rounding"){
        CHECK(esp_cpu_task_stack_floor_v1(&l,working,&output));CHECK(output==1296);
        l={0,1,1,2,1,1,16};CHECK(esp_cpu_task_stack_floor_v1(&l,1,&output));CHECK(output==80);
        l={0,0,0,0,0,1,1};CHECK(esp_cpu_task_stack_floor_v1(&l,1,&output));CHECK(output==3);return;
    }
    if(name=="floor-empty-tls"){
        l={0,0,0,0,0,16,16};CHECK(esp_cpu_task_stack_floor_v1(&l,1,&output));CHECK(output==48);return;
    }
    if(name=="floor-port-formula"){
        for(unsigned residue=0;residue<16;++residue)
        for(uint32_t cp: {0u,12u,48u,65u})
        for(uint32_t data: {0u,1u,15u,16u,17u,63u})
        for(uint32_t bss: {0u,1u,15u,16u,17u,65u})
        for(uint32_t budget: {1u,15u,16u,17u,1024u}){
            l={0x1000,0x1000+data,0x2000,0x2000+bss,cp,160,16};
            CHECK(esp_cpu_task_stack_floor_v1(&l,budget,&output));
            CHECK(port_writes_fit(l,output,budget,residue));
        }
        // A concrete undersized allocation violates the modeled actual writes.
        l=current_layout;CHECK(!port_writes_fit(l,16,1024,1));
        CHECK(esp_cpu_task_stack_floor_v1(&l,1024,&output));
        for(unsigned residue=0;residue<16;++residue)CHECK(port_writes_fit(l,output,1024,residue));
        CHECK(!port_writes_fit(l,output-1,1024,1));
        // Isolate setup-frame underflow: even a one-byte working demand cannot
        // fit when the total is only the initial frame size plus one byte.
        l={0,0,0,0,0,160,16};CHECK(!port_writes_fit(l,161,1,1));return;
    }
    if(name=="floor-zero-budget")working=0;
    else if(name=="floor-zero-frame")l.initial_frame_bytes=0;
    else if(name=="floor-zero-alignment")l.alignment=0;
    else if(name=="floor-nonpower-alignment")l.alignment=3;
    else if(name=="floor-reversed-data")l.tls_data_end=l.tls_data_start-1;
    else if(name=="floor-reversed-bss")l.tls_bss_end=l.tls_bss_start-1;
    else if(name=="floor-overlap")l.tls_bss_start=l.tls_data_end-1;
    else if(name=="floor-span-overflow"){
        CHECK(sizeof(uintptr_t)>4);l={0,uintptr_t(UINT32_MAX)+1,uintptr_t(UINT32_MAX)+1,uintptr_t(UINT32_MAX)+1,0,16,16};
    }
    else if(name=="floor-tls-sum-overflow")l={0,UINT32_MAX,UINT32_MAX,uintptr_t(UINT32_MAX)+1,0,16,16};
    else if(name=="floor-round-cp-overflow")l.coprocessor_bytes=UINT32_MAX;
    else if(name=="floor-round-tls-overflow")l={0,UINT32_MAX,UINT32_MAX,UINT32_MAX,0,16,16};
    else if(name=="floor-round-frame-overflow")l.initial_frame_bytes=UINT32_MAX;
    else if(name=="floor-round-working-overflow")working=UINT32_MAX;
    else if(name=="floor-total-overflow")l={0,0,0,0,0xfffffff0u,16,16};
    else CHECK(false);
    CHECK(!esp_cpu_task_stack_floor_v1(&l,working,&output));CHECK(output==0x76543210);
}
