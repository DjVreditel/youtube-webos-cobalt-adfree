#!/usr/bin/env python3
"""Test the real AAC loader; optionally decode audio with the real FDK library."""
import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('cobalt', type=Path)
parser.add_argument('--library', type=Path)
args = parser.parse_args()
cobalt = args.cobalt.resolve()
spec = importlib.util.spec_from_file_location('methods', ROOT / 'scripts/test-external-video-seek.py')
methods = importlib.util.module_from_spec(spec)
spec.loader.exec_module(methods)
adapter = (cobalt / 'starboard/shared/libfdkaac/fdk_aac_audio_decoder.cc').read_text()
actual = '\n'.join(methods.method(adapter, signature) for signature in [
    'void FdkAacAudioDecoder::InitializeCodec(', 'void FdkAacAudioDecoder::TeardownCodec(',
    'bool FdkAacAudioDecoder::WriteToFdkDecoder(', 'bool FdkAacAudioDecoder::ReadFromFdkDecoder(',
    'void FdkAacAudioDecoder::TryToUpdateStreamInfo('])
fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "starboard/common/log.h"
#include "starboard/shared/libfdkaac/libfdkaac_library_loader.h"
// Use the actual decoding operations, with scheduling/output ownership
// replaced only at the fixture boundary.
namespace starboard { namespace shared { namespace libfdkaac {
template<class T> using scoped_refptr = std::shared_ptr<T>;
struct InputBuffer {
  std::vector<unsigned char> bytes;
  const unsigned char* data() const { return bytes.data(); }
  unsigned int size() const { return bytes.size(); }
};
constexpr int kSbPlayerErrorDecode=0;
std::string FormatString(const char* format,int value) {
  char s[160];std::snprintf(s,sizeof(s),format,value);return s;
}
struct FdkAacAudioDecoder {
  enum DecodeMode { kDecodeModeFlush, kDecodeModeDoNotFlush };
  static constexpr int kMaxOutputBufferSizeInBytes=sizeof(int16_t)*2048*8;
  HANDLE_AACDECODER decoder_=nullptr;
  uint8_t output_buffer_[kMaxOutputBufferSizeInBytes]{};
  bool has_stream_info_=false;
  int num_channels_=0,samples_per_second_=0;
  size_t decoded_audio_size_in_bytes_=0,audio_data_to_discard_in_bytes_=0;
  int outputs=0;uint64_t energy=0;
  std::function<void(int,const std::string&)> error_cb_=[](int,const std::string&){assert(false);};
  bool BelongsToCurrentThread() const { return true; }
  void InitializeCodec(); void TeardownCodec(); void TryToUpdateStreamInfo();
  bool WriteToFdkDecoder(const scoped_refptr<InputBuffer>&);
  bool ReadFromFdkDecoder(DecodeMode);
  void TryToOutputDecodedAudio(const uint8_t* data,int bytes) {
    ++outputs;
    for(int i=0;i+1<bytes;i+=2){int16_t v;memcpy(&v,data+i,2);energy+=v<0?-int(v):int(v);}
  }
};
REAL_METHODS
}}}
// Keep firmware lookup under fixture control even on a host that has FDK
// installed. The loader's private ORIGIN path still uses real glibc loading.
extern "C" void* __real_dlopen(const char*, int);
extern "C" void* __wrap_dlopen(const char* name, int flags) {
  if(std::string(name)=="libfdk-aac.so") {
    const char* firmware=std::getenv("YTAF_TEST_FIRMWARE_LIBRARY");
    assert(firmware);return __real_dlopen(firmware,flags);
  }
  return __real_dlopen(name,flags);
}
int main(int argc,char** argv) {
  using namespace starboard::shared::libfdkaac;
  assert(argc>=2);
  const std::string mode=argv[1];
  const bool loaded=LibfdkaacHandle::GetHandle()->IsLoaded();
  if(mode=="missing"){assert(!loaded);return 0;}
  assert(loaded);
  Dl_info info{};assert(dladdr(reinterpret_cast<void*>(starboard::shared::libfdkaac::aacDecoder_Open),&info));
  if(mode=="private" || mode=="decode")
    assert(std::string(info.dli_fname).find("/app/lib/")!=std::string::npos);
  if(mode!="decode")return 0;
  assert(argc==3);
  std::ifstream in(argv[2],std::ios::binary);
  const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in),{}};
  assert(!bytes.empty());
  for(int pass=0;pass<2;++pass){
    FdkAacAudioDecoder decoder;decoder.InitializeCodec();int frames=0;
    for(size_t offset=0;offset<bytes.size();){
      assert(offset+7<=bytes.size());const auto* p=bytes.data()+offset;
      assert(p[0]==0xff && (p[1]&0xf0)==0xf0);
      const size_t size=((p[3]&3)<<11)|(p[4]<<3)|(p[5]>>5);
      assert(size>=7 && offset+size<=bytes.size());
      auto packet=std::make_shared<InputBuffer>();packet->bytes.assign(p,p+size);
      assert(decoder.WriteToFdkDecoder(packet));
      assert(decoder.ReadFromFdkDecoder(FdkAacAudioDecoder::kDecodeModeDoNotFlush));
      offset+=size;++frames;
    }
    assert(frames>8 && decoder.outputs>4 && decoder.energy>0);
    assert(decoder.num_channels_==2 && decoder.samples_per_second_==48000);
    decoder.TeardownCodec();assert(!decoder.decoder_);
  }
  puts("Real FDK-AAC decoded stereo AAC-LC through Cobalt feed/decode/output-delay methods twice");
}
'''
with tempfile.TemporaryDirectory(prefix='ytaf-aac-loader-') as directory:
    root = Path(directory)
    app, firmware, stubs = (root / name for name in ['app', 'firmware', 'stubs'])
    (app / 'lib').mkdir(parents=True)
    firmware.mkdir()
    for name, source in {
        'starboard/common/log.h': '#pragma once\n#include <cassert>\n#include <sstream>\n#define SB_DCHECK(v) assert(v)\n#define SB_LOG(v) std::ostringstream()\n',
        'starboard/shared/internal_only.h': '#pragma once\n',
        'starboard/once.h': '#pragma once\n#define SB_ONCE_INITIALIZE_FUNCTION(T,F) T* F(){ static T instance; return &instance; }\n',
    }.items():
        p = stubs / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(source)
    cpp, binary = root / 'test.cc', app / 'test'
    cpp.write_text(fixture.replace('REAL_METHODS', actual))
    compiler = os.environ.get('CXX', 'c++')
    subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-DSTARBOARD_WEBOS',
                    '-I'+str(stubs), '-I'+str(cobalt), str(cpp),
                    str(cobalt / 'starboard/shared/libfdkaac/libfdkaac_library_loader.cc'),
                    '-ldl', '-Wl,--wrap=dlopen', '-o', str(binary)], check=True)
    env = dict(os.environ, LD_LIBRARY_PATH=str(firmware),
               YTAF_TEST_FIRMWARE_LIBRARY=str(firmware / 'libfdk-aac.so'))
    subprocess.run([str(binary), 'missing'], env=env, check=True)
    mock = root / 'mock.cc'
    symbols = ['GetStreamInfo', 'Close', 'Open', 'ConfigRaw', 'SetParam', 'AncDataInit', 'Fill', 'DecodeFrame']
    mock.write_text('\n'.join('extern "C" void aacDecoder_'+s+'() {}' for s in symbols))
    private = app / 'lib/libfdk-aac.so'
    subprocess.run([compiler, '-shared', '-fPIC', str(mock), '-o', str(private)], check=True)
    subprocess.run([str(binary), 'private'], env=env, check=True)
    shutil.move(private, firmware / private.name)
    subprocess.run([str(binary), 'fallback'], env=env, check=True)
    # A packaged decoder takes priority over a present but incompatible system library.
    shutil.copyfile(firmware / private.name, private)
    mock.write_text('extern "C" void aacDecoder_Open() {}\n')
    subprocess.run([compiler, '-shared', '-fPIC', str(mock), '-o', str(firmware / private.name)], check=True)
    subprocess.run([str(binary), 'private'], env=env, check=True)
    shutil.copyfile(firmware / private.name, private)
    subprocess.run([str(binary), 'missing'], env=env, check=True)
    if args.library:
        shutil.copyfile(args.library, private)
        subprocess.run([str(binary), 'decode', str(ROOT / 'scripts/fixtures/aac/stereo-48k.aac')],
                       env=env, check=True)
print('AAC loader: private dependency, firmware fallback, symbol rejection and preference passed')
