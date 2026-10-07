// These are native ownership records, including failed partial channel setup.
int pwmPins[4]={-1,-1,-1,-1};
bool pwmOwnedPins[49]{};
inline bool stopPwm(uint8_t pin){
  // Ordinary GPIO transitions need no LEDC scan, even while another pin owns
  // PWM. Only successful cleanup may clear this per-pin ownership guard.
  if(pin<49 && !pwmOwnedPins[pin])return true;
  for(int i=0;i<4;++i)if(pwmPins[i]==pin){
    if(ledc_stop(LEDC_LOW_SPEED_MODE,static_cast<ledc_channel_t>(i),0)!=ESP_OK)return false;
    esp_rom_gpio_connect_out_signal(pin,SIG_GPIO_OUT_IDX,false,false);pwmPins[i]=-1;
  }
  if(pin<49)pwmOwnedPins[pin]=false;
  return true;
}
