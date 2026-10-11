// Deliberately independent of Hardware.sleep and its diagnostics/performance
// work. Cleanup may cooperate after native custody becomes retained.
bool schedulerWait(uint32_t ms){
  if(xPortInIsrContext() || !ownerTask || !ownerTask() || !ms || ms>RISC_PLATFORM_CLOCK_WAIT_MAX_MS)return false;
  vTaskDelay(cooperativeDelayTicks(ms,configTICK_RATE_HZ));return true;
}
void configureSchedulerWait(Hardware& hardware){hardware.schedulerWait=schedulerWait;}
