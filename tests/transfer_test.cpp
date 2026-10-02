#include "transfer.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main(){
 Transfer t;
 assert(t.command("HELLO")=="JANUS 1 65536");
 assert(t.command("INFO")=="ERR EMPTY");

 // CRC32 corpus (canonical values; see tests/crc_cross_check.py for the
 // Python-reference equivalents, and tests/janus_ps1_tests.ps1 for the
 // PowerShell equivalents of the same vectors).
 assert(crc32(nullptr, 0) == 0x00000000u);
 assert(crc32(reinterpret_cast<const uint8_t*>("a"), 1) == 0xe8b7be43u);
 assert(crc32(reinterpret_cast<const uint8_t*>("abc"), 3) == 0x352441c2u);
 assert(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926u);
 {
  const char fox[] = "The quick brown fox jumps over the lazy dog";
  assert(crc32(reinterpret_cast<const uint8_t*>(fox), sizeof(fox) - 1) == 0x414fa339u);
 }
 {
  uint8_t ff256[256]; std::memset(ff256, 0xff, sizeof(ff256));
  assert(crc32(ff256, sizeof(ff256)) == 0xfea8a821u);
 }
 {
  uint8_t range256[256]; for (int i = 0; i < 256; ++i) range256[i] = static_cast<uint8_t>(i);
  assert(crc32(range256, sizeof(range256)) == 0x29058c73u);
 }
 assert(t.command("BEGIN 3 891568578")=="OK BEGIN");
 assert(t.command("DATA 1 61")=="ERR OFFSET");
 assert(t.command("DATA 0 zz")=="ERR HEX");
 assert(t.command("DATA 0 616263")=="OK DATA 3");
 assert(t.command("COMMIT")=="OK COMMIT");
 assert(t.command("SDINFO")=="ERR SD");
 assert(t.command("READ 0 3")=="DATA 616263");
 assert(t.command("BEGIN 1 0")=="OK BEGIN");
 assert(t.command("DATA 0 64")=="OK DATA 1");
 assert(t.command("COMMIT")=="ERR CRC");
 assert(t.command("READ 0 3")=="DATA 616263");
 assert(t.command("READ 65536 1")=="ERR RANGE");
 assert(t.command("BEGIN 65537 0")=="ERR SIZE");
 assert(t.command("BEGIN 0 0")=="OK BEGIN");
 assert(t.command("COMMIT")=="OK COMMIT");
 assert(t.command("INFO")=="FILE 0 0");

 // ABORT clears in-progress state and leaves the committed file intact.
 // Post-ABORT, DATA and COMMIT return ERR STATE (not ERR OFFSET / ERR
 // INCOMPLETE): the distinction lets the host tell "no BEGIN in progress"
 // apart from "wrong offset" and "size announced vs sent disagreed".
 assert(t.command("BEGIN 3 0")=="OK BEGIN");
 assert(t.command("DATA 0 61")=="OK DATA 1");
 assert(t.command("ABORT")=="OK ABORT");
 assert(t.command("DATA 1 62")=="ERR STATE");
 assert(t.command("COMMIT")=="ERR STATE");
 assert(t.command("INFO")=="FILE 0 0");

 // SDDIAG is wired through the dispatcher on every build; host builds say so.
 assert(t.command("SDDIAG")=="DIAG UNSUPPORTED");

 // Re-BEGIN mid-transfer must discard partial staging, not silently resume.
 const uint8_t zy[]={'z','y'};
 const auto zy_crc=crc32(zy,sizeof(zy));
 assert(t.command("BEGIN 4 0")=="OK BEGIN");
 assert(t.command("DATA 0 6162")=="OK DATA 2");
 assert(t.command("BEGIN 2 "+std::to_string(zy_crc))=="OK BEGIN");
 assert(t.command("DATA 2 7a79")=="ERR OFFSET");
 assert(t.command("DATA 0 7a79")=="OK DATA 2");
 assert(t.command("COMMIT")=="OK COMMIT");
 assert(t.command("INFO")=="FILE 2 "+std::to_string(zy_crc));

 std::cout<<"Protocol tests passed\n";
}
