// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12];
  ushort type; //file type
  ushort machine;//architecture
  uint version;//file version
  uint64 entry;//entry for the prog
  uint64 phoff;//program hdr table offset in the file
  uint64 shoff;//section header table offset
  uint flags;//IA32=0//remain
  ushort ehsize;//elf header size
  ushort phentsize;//size of a single entry program header table 
  ushort phnum;//num of entries in program header table
  ushort shentsize;//size of a single entry section header table 
  ushort shnum;//num of ... in secion
  ushort shstrndx;//sring which include the name of section in which section(start with 0)
};

// Program section header
struct proghdr {
  uint32 type; //section type
  uint32 flags;
  uint64 off;//first byte of section off
  uint64 vaddr;//f b of s :: virtual addr
  uint64 paddr;//phyical (remain for old machine)
  uint64 filesz;//length of section 
  uint64 memsz;//length of section in memory (may not as same as filesz/larger, file zero, not read)
  uint64 align;//specific how to align section memory/file 
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
