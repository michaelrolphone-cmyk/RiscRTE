// Appended to the pinned real X4 provider/CpuPort fixture in a temporary tree.
static bool busy() { return false; }
static bool holdOwner=true, failHold=false, failUnhold=false, reenterHold=false;
static unsigned nativeHoldCalls=0;
static Port* holdPort=nullptr;
static void scopedHoldControls() {
    Hardware h{};
    h.owner=[](){return holdOwner;};
    h.deepHold=[](uint8_t pin,bool enable){
        assert(pin==8);++nativeHoldCalls;
        if(reenterHold)assert(Port::gpioDeepSleepHold(&holdPort->gpios_[0],101,enable)==RISC_DEEP_SLEEP_BUSY);
        return !(enable?failHold:failUnhold);
    };
    Port p{h};holdPort=&p;
    auto& g=p.gpios_[0];g.port=&p;g.instance=1;
    auto& other=p.gpios_[1];other.port=&p;other.instance=2;
    auto& pin=p.pins_[8];pin.owner=&g;pin.token=101;pin.output=true;
    auto call=[&](bool enable){return Port::gpioDeepSleepHold(&g,101,enable);};
    assert(Port::gpioDeepSleepHold(nullptr,101,true)==RISC_DEEP_SLEEP_INVALID);
    assert(Port::gpioDeepSleepHold(&other,101,true)==RISC_DEEP_SLEEP_INVALID);
    assert(Port::gpioDeepSleepHold(&g,102,true)==RISC_DEEP_SLEEP_INVALID);
    holdOwner=false;assert(call(true)==RISC_DEEP_SLEEP_CONTEXT);holdOwner=true;
    pin.output=false;assert(call(true)==RISC_DEEP_SLEEP_INVALID);pin.output=true;
    pin.pwm=true;assert(call(true)==RISC_DEEP_SLEEP_BUSY);pin.pwm=false;
    p.sleeping_=true;assert(call(true)==RISC_DEEP_SLEEP_BUSY);p.sleeping_=false;
    p.transferring_=true;assert(call(true)==RISC_DEEP_SLEEP_BUSY);p.transferring_=false;
    p.sleepRetained_=true;assert(call(true)==RISC_DEEP_SLEEP_RETAINED);p.sleepRetained_=false;
    assert(nativeHoldCalls==0);
    p.nativeStorageLease_=71;p.storageWork_.gpio=&g;
    assert(call(true)==RISC_DEEP_SLEEP_BUSY && nativeHoldCalls==0);
    p.storageWork_.gpio=&other;
    assert(call(true)==0 && pin.held);
    p.nativeStorageLease_=0;p.storageWork_.gpio=nullptr;
    assert(call(false)==0 && !pin.held);
    reenterHold=true;assert(call(true)==0 && pin.held);reenterHold=false;
    assert(call(false)==0 && !pin.held);
    failHold=true;
    assert(call(true)==RISC_DEEP_SLEEP_PLATFORM && !p.poisoned_ && !pin.held);
    failUnhold=true;
    assert(call(true)==RISC_DEEP_SLEEP_RETAINED && p.poisoned_ && p.sleepRetained_ && pin.held);
    const auto calls=nativeHoldCalls;
    assert(call(false)==RISC_DEEP_SLEEP_RETAINED && nativeHoldCalls==calls);
    puts("PAD_HOLD: scope/token/PWM/owner/storage/reentry and native rollback controls PASS");
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    baseline_main();
    const bool fixed=std::getenv("PAD_HOLD_EXPECT_BROKEN")==nullptr;
    for(unsigned kind=0;kind<5;++kind) {
        Fixture f;
        auto setBusy=[&](bool value){
            auto callback=value?busy:nullptr;
            if(kind==0)f.port.hw_.maintenanceIdle=callback;
            if(kind==1)f.port.hw_.usbPhyIdle=callback;
            if(kind==2)f.port.hw_.httpIdle=callback;
            if(kind==3){f.port.iq_.token=value?77:0;f.port.iq_.worker=value;}
            if(kind==4){
                auto& storage=f.port.gpios_[1];storage.port=&f.port;storage.instance=2;
                f.port.nativeStorageLease_=value?71:0;
                f.port.storageWork_.gpio=value?&storage:nullptr;
            }
        };
        setBusy(true);
        const bool began=f.start(), cleaned=driver->quiesce();
        std::printf("PAD_HOLD repaired=%d busy_kind=%u start=%d quiesce=%d\n",fixed,kind,began,cleaned);
        assert(began==fixed && cleaned==fixed);
        assert(!f.port.appExitSafe());
        if(kind!=0)assert(!f.port.restartResourcesSafe());
        setBusy(false);assert(driver->quiesce());f.clean();
        assert(f.start());assert(frontlight->set_level(nullptr,1,2));
        setBusy(true);assert(frontlight->set_level(nullptr,0,1)==fixed);
        setBusy(false);assert(driver->quiesce());f.clean();
    }
    if(fixed)scopedHoldControls();
    puts("PAD_HOLD: production frontlight startup/off/cleanup regression PASS");
    return 0;
}
