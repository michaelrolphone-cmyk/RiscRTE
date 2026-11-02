#pragma once
// Test-only literal LZ4 envelope; production codec remains unchanged.
static std::vector<uint8_t> bankEnvelope(const std::vector<uint8_t>& bytes){
 std::vector<uint8_t> out(32,0);memcpy(out.data(),"RTEIMG1\0",8);
 const auto word=[&](size_t at,uint32_t value){for(unsigned i=0;i<4;++i)out[at+i]=uint8_t(value>>(8*i));};
 uint32_t crc=~uint32_t(0);for(uint8_t b:bytes){crc^=b;for(unsigned k=0;k<8;++k)crc=(crc>>1)^(0xedb88320u&uint32_t(0-int32_t(crc&1)));}
 word(8,32);word(12,1);word(16,uint32_t(bytes.size()));word(24,~crc);
 out.push_back(uint8_t(std::min<size_t>(15,bytes.size())<<4));
 if(bytes.size()>=15){size_t left=bytes.size()-15;while(left>=255){out.push_back(255);left-=255;}out.push_back(uint8_t(left));}
 out.insert(out.end(),bytes.begin(),bytes.end());word(20,uint32_t(out.size()-32));return out;
}
