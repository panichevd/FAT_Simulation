#ifndef FS_H
#define FS_H

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <list>
#include <map>
#include <vector>

using namespace std;

const unsigned int DISK_SIZE = 1024*1024*1024;

class DirectoryEntry
{
private:
    bool m_directory;
    string m_name;
    int m_first_block;
    unsigned int m_size;
public:
    const static unsigned int DIRECTORY_ENTRY_SIZE = 32;

    DirectoryEntry(const unsigned char *data);    
    DirectoryEntry(bool directory, const char *name, int first_block, unsigned int size);    
    ~DirectoryEntry();
    
    void         get_data(unsigned char *buffer) const;
    const char * get_name()                      const { return m_name.c_str(); } 
    int          get_first_block()               const { return m_first_block; }
    unsigned int get_size()                      const { return m_size; }

    void increase_size(unsigned int diff) { m_size += diff; }
};

class FAT
{
private:
    const static unsigned int BLOCK_SIZE = 4*1024;
    const static unsigned int TABLE_SIZE = DISK_SIZE/BLOCK_SIZE;
    const static unsigned int ROOT_OFFSET = TABLE_SIZE*sizeof(int) + TABLE_SIZE*sizeof(bool);
    const static unsigned int DATA_OFFSET = ROOT_OFFSET + BLOCK_SIZE; 

    struct OpenedFile
    {
        string path;
        unsigned int file_pointer;
        int first_block;
        int current_block;
        unsigned int size;
    };

    bool m_init;
    int m_fd;
    int m_table[TABLE_SIZE];
    bool m_free_blocks[TABLE_SIZE];
    DirectoryEntry *m_root;

    unsigned int m_next_descriptor;
    map<unsigned int, OpenedFile> m_opened_files;

    int open_file(const char *path, OpenedFile &file);
    int increase_size(const char *path, unsigned int diff);
    int get_next_file_block(int current_block);

    int get_next_free_block();
    bool create_root_directory();
    bool update_fs_structs();
public:
    FAT(const char * name);
    ~FAT();    

    bool initialized() const { return m_init; }

    int create(const char *path);
    int open(const char *path);
    void close(int fd);

    int lseek(int fd, int offset, int whence);
    int read(int fd, void *data, size_t nbyte);
    int write(int fd, void *data, size_t nbyte);
};

#endif
