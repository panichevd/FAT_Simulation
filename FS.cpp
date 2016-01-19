#include "FS.h"

DirectoryEntry::DirectoryEntry(bool directory, const char *name, int first_block, unsigned int size) :
    m_directory  (directory),
    m_name       (name),
    m_first_block(first_block),
    m_size       (size)
{
}

DirectoryEntry::DirectoryEntry(const unsigned char * data) :
    m_directory(*(bool*)data),
    m_name((const char*)data + 1, 8),
    m_first_block(*(int*)(data + 9)),
    m_size(*((unsigned int*)(data + 9 + sizeof(int))))
{
}

DirectoryEntry::~DirectoryEntry()
{
}

void DirectoryEntry::get_data(unsigned char *buffer) const
{
    memset(buffer, 0, DIRECTORY_ENTRY_SIZE);
    buffer[0] = m_directory;
    for (unsigned int i = 0; i < m_name.size() && i < 8; ++i)
        buffer[i + 1] = *(m_name.c_str() + i);
    *(int*)(buffer + 1 + 8) = m_first_block;
    *(unsigned int *)(buffer + 1 + 8 + sizeof(int)) = m_size;
}

FAT::FAT(const char * path) : m_init(false), m_next_descriptor(0)
{
    for (unsigned int i = 0; i < TABLE_SIZE; ++i)
    {
        m_table[i] = -1;
        m_free_blocks[i] = false;
    }

    m_fd = ::open(path, O_RDWR);
    if (m_fd != -1)
    {
        if (::read(m_fd, m_table, TABLE_SIZE*sizeof(unsigned int)) != TABLE_SIZE*sizeof(unsigned int))
            return;

        if (::read(m_fd, m_free_blocks, TABLE_SIZE*sizeof(bool)) != TABLE_SIZE*sizeof(bool))
            return;

        unsigned char buffer[DirectoryEntry::DIRECTORY_ENTRY_SIZE];
        if (::read(m_fd, buffer, DirectoryEntry::DIRECTORY_ENTRY_SIZE) != DirectoryEntry::DIRECTORY_ENTRY_SIZE)
            return;
        m_root = new DirectoryEntry(buffer);
    }
    else
    {
        m_fd = ::open(path, O_RDWR | O_CREAT);
        if (m_fd != -1)
        {
            if (::write(m_fd, m_table, TABLE_SIZE*sizeof(unsigned int)) != TABLE_SIZE*sizeof(unsigned int))
                return;

            if (::write(m_fd, m_free_blocks, TABLE_SIZE*sizeof(bool)) != TABLE_SIZE*sizeof(bool))
                return;

            if (!create_root_directory())
                return;

        }
        else return;
    }
    m_init = true;
}

FAT::~FAT()
{
    if (m_fd != -1)
        close(m_fd);
}

bool FAT::create_root_directory()
{
    unsigned char buffer[BLOCK_SIZE] = { 0 };

    m_root = new DirectoryEntry(true, "", 0, 2*DirectoryEntry::DIRECTORY_ENTRY_SIZE);
    m_root->get_data(buffer);
    if (::write(m_fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
        return false;

    DirectoryEntry cur_dir(true, ".", 0, 2*DirectoryEntry::DIRECTORY_ENTRY_SIZE);
    DirectoryEntry up(true, "..", 0, 2*DirectoryEntry::DIRECTORY_ENTRY_SIZE);

    //  Write root to 1st block
    m_free_blocks[0] = true;
    cur_dir.get_data(buffer);
    up.get_data(buffer + DirectoryEntry::DIRECTORY_ENTRY_SIZE);
    if (::write(m_fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
        return false;

    lseek(m_fd, TABLE_SIZE*sizeof(unsigned int), SEEK_SET);
    if (::write(m_fd, m_free_blocks, BLOCK_SIZE) != BLOCK_SIZE)
        return false;

    return true;
}

int FAT::open_file(const char *path, OpenedFile &opened_file)
{
    int block = m_root->get_first_block();
    unsigned int size = m_root->get_size();
    unsigned char buffer[BLOCK_SIZE];

    while (true)
    {
        lseek(m_fd, DATA_OFFSET + block*BLOCK_SIZE, SEEK_SET);
        unsigned int read_size = (size > BLOCK_SIZE) ? BLOCK_SIZE : size;
        if (::read(m_fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
            return -1;

        for (unsigned int i = 0; i < read_size; i += DirectoryEntry::DIRECTORY_ENTRY_SIZE)
        {
            DirectoryEntry file(buffer + i);
            if (string(path).substr(0, 8) == file.get_name())
            {
                opened_file = { path, 0, file.get_first_block(), file.get_first_block(), file.get_size() };
                return 0;
            }
        }

        block = m_table[block];
        if (block == -1)
            break;

        size -= BLOCK_SIZE;
    }

    return -1;
}

int FAT::get_next_free_block()
{
    for (int i = 0; i < TABLE_SIZE; ++i)
    {
        if (!m_free_blocks[i])
        {
            m_free_blocks[i] = true;
            return i;
        }
    }
}

int FAT::increase_size(const char *path, unsigned int diff)
{
    int block = m_root->get_first_block();
    unsigned int size = m_root->get_size();
    unsigned char buffer[BLOCK_SIZE];

    while (true)
    {
        lseek(m_fd, DATA_OFFSET + block*BLOCK_SIZE, SEEK_SET);
        unsigned int read_size = (size > BLOCK_SIZE) ? BLOCK_SIZE : size;
        if (::read(m_fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
            return -1;

        for (unsigned int i = 0; i < read_size; i += DirectoryEntry::DIRECTORY_ENTRY_SIZE)
        {
            DirectoryEntry file(buffer + i);
            if (string(path).substr(0, 8) == file.get_name())
            {
                file.increase_size(diff);
                file.get_data(buffer + i);
                ::lseek(m_fd, -static_cast<int>(BLOCK_SIZE), SEEK_CUR);
                return ::write(m_fd, buffer, BLOCK_SIZE);
            }
        }

        block = m_table[block];
        if (block == -1)
            break;

        size -= BLOCK_SIZE;
    }

    return -1;

}

bool FAT::update_fs_structs()
{
    lseek(m_fd, 0, SEEK_SET);

    if (::write(m_fd, m_table, TABLE_SIZE*sizeof(unsigned int)) != TABLE_SIZE*sizeof(unsigned int))
        return false;

    if (::write(m_fd, m_free_blocks, TABLE_SIZE*sizeof(bool)) != TABLE_SIZE*sizeof(bool))
        return false;

    unsigned char buffer[BLOCK_SIZE];
    m_root->get_data(buffer);
    if (::write(m_fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
        return false;

    return true;
}

int FAT::create(const char *path)
{
    int block = m_root->get_first_block();
    unsigned int size = m_root->get_size();
    unsigned char buffer[BLOCK_SIZE];

    while (true)
    {
        lseek(m_fd, DATA_OFFSET + block*BLOCK_SIZE, SEEK_SET);
        unsigned int read_size = (size > BLOCK_SIZE) ? BLOCK_SIZE : size;
        if (::read(m_fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
            return -1;

        for (unsigned int i = 0; i < read_size; i += DirectoryEntry::DIRECTORY_ENTRY_SIZE)
        {
            DirectoryEntry file(buffer + i);
            if (string(path).substr(0, 8) == file.get_name())
                return -1;
        }

        block = m_table[block];
        if (block == -1)
            break;

        size -= BLOCK_SIZE;
    }

    int file_block = get_next_free_block();
    DirectoryEntry new_file(false, path, file_block, 0);

    if (size == BLOCK_SIZE)
    {
        int new_block = get_next_free_block();
        m_table[block] = new_block;
        lseek(m_fd, DATA_OFFSET + new_block*BLOCK_SIZE, SEEK_SET);
        size = 0;
    }
    else
        lseek(m_fd, -static_cast<int>(BLOCK_SIZE), SEEK_CUR);

    // Write directory changes
    new_file.get_data(buffer + size);
    if (::write(m_fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
        return -1;

    // Create empty file
    memset(buffer, 0, BLOCK_SIZE);
    ::lseek(m_fd, DATA_OFFSET + file_block*BLOCK_SIZE, SEEK_SET);
    if (::write(m_fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
        return -1;

    m_root->increase_size(DirectoryEntry::DIRECTORY_ENTRY_SIZE);
    if (!update_fs_structs())
        return -1;

    return 0;
}

int FAT::open(const char *path)
{
    for (auto it = m_opened_files.begin(); it != m_opened_files.end(); ++it)
    {
        if (it->second.path == path)
            return -1;
    }

    OpenedFile of;
    if (open_file(path, of) == -1)
        return -1;

    m_opened_files[m_next_descriptor] = of;
    return m_next_descriptor++;
}

void FAT::close(int fd)
{
    m_opened_files.erase(fd);
}

// TODO: file pointer moving
// TODO: check of last block in loop
int FAT::write(int fd, void *data, size_t nbyte)
{
    auto it = m_opened_files.find(fd);
    if (it == m_opened_files.end())
        return -1;

    char buffer[BLOCK_SIZE];
    unsigned int written = 0;

    unsigned int left = BLOCK_SIZE - it->second.file_pointer % BLOCK_SIZE;
    if (it->second.file_pointer > 0)
        left = 0;

    if (left > 0)
    {
        ::lseek(m_fd, DATA_OFFSET + it->second.current_block*BLOCK_SIZE, SEEK_SET);
        if (::read(m_fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
            return -1;
        
        memcpy(buffer + it->second.file_pointer % BLOCK_SIZE, data, (nbyte <= left) ? nbyte: left);
        ::lseek(m_fd, DATA_OFFSET + it->second.current_block*BLOCK_SIZE, SEEK_SET);
        if (::write(m_fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
            return -1;

        written += (nbyte <= left) ? nbyte: left;
        it->second.file_pointer += written;

        if (nbyte <= left)
            return increase_size(it->second.path.c_str(), written);
    }

    while (written != nbyte)
    {
        int new_block = get_next_free_block();
        m_table[it->second.current_block] = new_block;
        it->second.current_block = new_block;
        ::lseek(m_fd, DATA_OFFSET + it->second.current_block*BLOCK_SIZE, SEEK_SET);

        left = nbyte - written;

        memset(buffer, 0, BLOCK_SIZE);
        memcpy(buffer, static_cast<unsigned char*>(data) + written, (left <= BLOCK_SIZE) ? left : BLOCK_SIZE);
        if (::write(m_fd, buffer, BLOCK_SIZE) != BLOCK_SIZE)
            return -1;
        written += (left <= BLOCK_SIZE) ? left : BLOCK_SIZE;
    }

    update_fs_structs();
    return 0;
}

int FAT::read(int fd, void *buffer, size_t nbyte)
{
    return -1;
}

int main()
{
    FAT fs("1");
    if (!fs.initialized())
        return -1;

    fs.create("new_fil2");
    int fd = fs.open("new_fil2");

    char buffer[5] = { 0, 18, 23, 1, 6} ;
    fs.write(fd, buffer, 5);
    fs.close(fd);

    return 0;
}
