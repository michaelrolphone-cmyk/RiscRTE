/* Actual Runtime startup and terminal callers. Demand activation and the app
 * exit/native-port barriers are deliberately not changed or bypassed. */
bool Runtime::startBootProvider(size_t i) {
  const auto& driver=drivers_[i];
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF && defined(RISC_CPU_SCOPE_V5_CANDIDATE) && RISC_CPU_SCOPE_V5_CANDIDATE == 1
  using Executor=RuntimePackages::DeviceProviderExecutorV2;
  if(Executor::selectedNativeOwner(graph_,driver.id,driver.provides,driver.api,driver.instance))
    return Executor::startNativeOwner(graph_,driver.id,driver.provides,driver.api,driver.instance,
                                       grants_[i],nativeOwners_[i]);
#endif
  grants_[i]=graph_.acquireFrom(driver.id,driver.provides,driver.api,driver.instance);
  return grants_[i].slot!=0;
}
bool Runtime::closeBootProvider(size_t i) {
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF && defined(RISC_CPU_SCOPE_V5_CANDIDATE) && RISC_CPU_SCOPE_V5_CANDIDATE == 1
  using Executor=RuntimePackages::DeviceProviderExecutorV2;
  if(nativeOwners_[i].state!=Executor::NativeState::Empty)
    return Executor::closeNativeOwner(graph_,grants_[i],nativeOwners_[i],
                                      RISC_NATIVE_TERMINAL_BUDGET_MAX_MS_V1);
#endif
  return graph_.release(grants_[i]);
}
