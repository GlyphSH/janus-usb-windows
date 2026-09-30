#include "transfer.h"
#include <cassert>
#include <iostream>
int main(){
 Transfer t;
 assert(t.command("HELLO")=="JANUS 1 65536");
 assert(t.command("INFO")=="ERR EMPTY");
 assert(crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926);
 assert(t.command("BEGIN 3 891568578")=="OK BEGIN");
 assert(t.command("DATA 1 61")=="ERR OFFSET");
 assert(t.command("DATA 0 zz")=="ERR HEX");
 assert(t.command("DATA 0 616263")=="OK DATA 3");
 assert(t.command("COMMIT")=="OK COMMIT");
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
 std::cout<<"Protocol tests passed\n";
}
