// Ten-bit LEDC period is 1024 ticks. Full endpoints use static GPIO levels.
inline bool pwmWrite(uint8_t pin,uint32_t hz,uint16_t duty,uint16_t maximum){
  if(pin>=49 || !hz || !maximum || duty>maximum)return false;
  if(duty==0 || duty==maximum)return gpioWrite(pin,duty==maximum);
  int slot=-1;
  for(int i=0;i<4;++i)if(pwmPins[i]==pin)slot=i;
  if(slot<0)for(int i=0;i<4;++i)if(pwmPins[i]<0){slot=i;break;}
  if(slot<0)return false;
  ledc_timer_config_t timer{};timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.duty_resolution=LEDC_TIMER_10_BIT;
  timer.timer_num=static_cast<ledc_timer_t>(slot);timer.freq_hz=hz;timer.clk_cfg=LEDC_AUTO_CLK;
  if(ledc_timer_config(&timer)!=ESP_OK)return false;
  ledc_channel_config_t channel{};channel.gpio_num=pin;channel.speed_mode=LEDC_LOW_SPEED_MODE;
  channel.channel=static_cast<ledc_channel_t>(slot);channel.intr_type=LEDC_INTR_DISABLE;
  channel.timer_sel=timer.timer_num;channel.duty=uint32_t(duty)*1024/maximum;
  pwmPins[slot]=pin; // Retain partial configuration until gpioClose can stop it.
  pwmOwnedPins[pin]=true;
  return ledc_channel_config(&channel)==ESP_OK;
}
