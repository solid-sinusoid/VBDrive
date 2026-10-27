#!/usr/bin/env python3
"""Check the production image CRC and GetInfo response on an independent fixture."""
from pathlib import Path
import subprocess
import tempfile
import os
import importlib.util

root=Path(__file__).resolve().parents[1]
source=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
#include <uavcan/node/GetInfo_1_0.h>
#include "firmware_crc.hpp"
int main(int argc,char** argv){
 const uint8_t known[]="123456789";
 assert(firmware_crc64(known,9)==UINT64_C(0x6C40DF5F0B497347));
 uint8_t image[]{0,1,2,3,255,255};
 const auto crc=firmware_crc64(image,sizeof(image));
 uavcan_node_GetInfo_Response_1_0 info{};
    NodeInfoReader reader;reader.set_image_crc(crc);info=reader.node_info;
 uint8_t encoded[uavcan_node_GetInfo_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_]{};
 size_t size=sizeof(encoded);
 assert(uavcan_node_GetInfo_Response_1_0_serialize_(&info,encoded,&size)==0);
 uavcan_node_GetInfo_Response_1_0 decoded{};
 assert(uavcan_node_GetInfo_Response_1_0_deserialize_(&decoded,encoded,&size)==0);
 assert(decoded.software_image_crc.count==1 && decoded.software_image_crc.elements[0]==crc);
 image[0]=1;assert(firmware_crc64(image,sizeof(image))!=crc);
 if(argc==2){
  std::ifstream stream(argv[1],std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),{});
  assert(bytes.size()<=0x1c800);
  bytes.resize(0x1c800,0xff);
  std::printf("%016llx\n",static_cast<unsigned long long>(firmware_crc64(bytes.data(),bytes.size())));
 }
}
'''
header=(root/'Drivers/libcxxcanard/cyphal/node/node_info_handler.h').read_text()
start=header.index('void set_image_crc(')
end=header.index('}',start)+1
source=source.replace('int main(int argc,char** argv){', 'struct NodeInfoReader { uavcan_node_GetInfo_Response_1_0 node_info{};\n'+header[start:end]+'\n};\nint main(int argc,char** argv){')
with tempfile.TemporaryDirectory(prefix='vbdrive-image-identity-') as d:
    p=Path(d);(p/'test.cpp').write_text(source)
    subprocess.run(['c++','-std=c++20','-I'+str(root/'App'),
                    '-I'+str(root/'build/Release/cyphal_types/c'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
    objcopy=os.environ.get('OBJCOPY','arm-none-eabi-objcopy')
    image=p/'firmware.bin'
    subprocess.run([objcopy,'-O','binary','--gap-fill','0xff',str(root/'build/Release/VBDrive.elf'),str(image)],check=True)
    data=image.read_bytes().ljust(0x1c800,b'\xff')
    assert len(data)==0x1c800
    spec=importlib.util.spec_from_file_location('boot_flasher',root/'VBBoot/tools/flash_bootloader_socketcan.py')
    flasher=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(flasher)
    hex_image=flasher.load_intel_hex(root/'build/Release/VBDrive.hex').ljust(0x1c800,b'\xff')
    assert data==hex_image
    expected=0
    for byte in data:
        expected^=byte<<56
        for _ in range(8):
            expected=((expected<<1)^(0x42F0E1EBA9EA3693 if expected&(1<<63) else 0))&0xffffffffffffffff
    actual=int(subprocess.check_output([str(p/'test'),str(image)],text=True),16)
    assert actual==expected
    print(f'Release image CRC-64: {actual:016X}')
print('PASS: CRC-64/ECMA-182 known vector and generated GetInfo nonempty real image CRC')
