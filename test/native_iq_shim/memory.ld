SECTIONS {
 .iq_reservation : {
  soc_reserved_memory_region_start = .;
  KEEP(*(.reserved_memory_address))
  soc_reserved_memory_region_end = .;
 }
}
INSERT AFTER .data;
PROVIDE(_heap_start = 0x3FCA8630);
PROVIDE(_iram_end = 0x40387000);
