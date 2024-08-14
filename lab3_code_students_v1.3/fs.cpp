#include <iostream>
#include "fs.h"


//Helpers
bool FS::readBlock(size_t blockNum, void* buffer) {
    uint8_t blk[BLOCK_SIZE];
    if (disk.read(blockNum, blk) == 0) {
        std::memcpy(buffer, blk, BLOCK_SIZE);
        return true;
    } else {
        std::cerr << "Error reading block " << blockNum << std::endl;
        return false;
    }
}

bool FS::writeBlock(size_t blockNum, const void* buffer) {
    uint8_t blk[BLOCK_SIZE];
    std::memcpy(blk, buffer, BLOCK_SIZE);
    if (disk.write(blockNum, blk) != 0) {
        std::cerr << "Error writing block " << blockNum << std::endl;
        return false;
    }
    return true;
}

//find list of free fat entris acording to the size of the file
std::vector<FATEntry> FS::freeFATEntries(uint8_t size) {
    std::vector<FATEntry> freeEntries;
    for (FATEntry i = 0; i < MAX_BLOCKS; ++i) {
        if (fat[i] == FAT_FREE) {
            std::cout << "Found free block: " << i << std::endl;
            std::cout << "enty: " << fat[i] << std::endl;
            freeEntries.push_back(i);
            if (freeEntries.size() == size) {
                break;
            }
        }
    }
    return freeEntries;
}
dir_entry FS::find_dir_block(const std::string& filepath) {
    uint8_t block[BLOCK_SIZE] = {0};
    disk.read(ROOT_BLOCK, block);
    dir_entry* dirblock = reinterpret_cast<dir_entry*>(block);

    size_t start_i = 0, end_i = 0;
    while ((end_i = filepath.find('/', start_i)) != std::string::npos) {
        std::string dirname = filepath.substr(start_i, end_i - start_i);
        bool found = false;
        for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
            if (dirname == dirblock[i].file_name && dirblock[i].type == TYPE_DIR) {
                disk.read(dirblock[i].first_blk, block);
                dirblock = reinterpret_cast<dir_entry*>(block);
                found = true;
                break;
            }
        }
        if (!found) return dir_entry(); // Not found or not a directory
        start_i = end_i + 1;
    }

    // Handle the last part of the path
    std::string filename = filepath.substr(start_i);
    for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (filename == dirblock[i].file_name) {
            return dirblock[i];
        }
    }

    return dir_entry(); // Not found
}

//System funktions
FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
}

FS::~FS()
{

}

// formats the disk, i.e., creates an empty file system
int
FS::format()
{
    uint8_t initBlock[BLOCK_SIZE] = { 0 };
    for (int i = 0; i < disk.get_no_blocks(); i++) {
        disk.write(i, initBlock);
    }

    std::string name("/");
    uint8_t block[BLOCK_SIZE] = { 0 };
    dir_entry* root = (dir_entry*)block;

    root->access_rights = READ | WRITE | EXECUTE;
    std::strncpy(root->file_name, name.c_str(), sizeof(root->file_name) - 1);
    root->file_name[sizeof(root->file_name) - 1] = '\0'; // Null-terminate

    root->first_blk = ROOT_BLOCK;
    root->size = 0; 
    root->type = TYPE_DIR; 
    
    std::fill(std::begin(fat) + 2, std::end(fat), FAT_FREE);
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;

    disk.write(ROOT_BLOCK, (uint8_t*)block);
    disk.write(FAT_BLOCK, (uint8_t*)fat);

    this->currentDir = ROOT_BLOCK;

    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
int FS::create(std::string filepath) {
    std::cout << "FS::create(" << filepath << ")\n";
    
    // Parse directory and file name from the given filepath
    size_t pos = filepath.find_last_of("/");
    std::string dirPath = filepath.substr(0, pos);
    std::string fileName = filepath.substr(pos + 1);
    if (fileName.size() > 55) {
        std::cerr << "Error: Invalid file name.\n";
        return -1;
    }

    // Locate the directory block where the file should be created
    dir_entry dirBlk = find_dir_block(dirPath);

    // Check if the file already exists in the directory
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(dirBlk.first_blk, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (std::strcmp(dirEntries[i].file_name, fileName.c_str()) == 0) {
            std::cerr << "Error: File '" << fileName << "' already exists.\n";
            return -1;
        }
    }

    // Capture the file content from user input
    std::string content;
    std::string line;
    size_t totalSize = 0;
    
    std::cout << "Enter the file content (end with an empty line):\n";
    while (true) {
        std::getline(std::cin, line);
        if (line.empty()) break;
        content += line + "\n";
        totalSize += line.length() + 1; // +1 for the newline character
    }

    // Calculate the number of blocks needed
    size_t requiredBlocks = (totalSize + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Find free FAT entries for the file
    std::vector<FATEntry> freeEntries = freeFATEntries(requiredBlocks);
    if (freeEntries.size() < requiredBlocks) {
        std::cerr << "Error: Not enough free blocks available.\n";
        return -1;
    }

    // Create a new directory entry
    dir_entry* newEntry = nullptr;
    for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (dirEntries[i].file_name[0] == '\0') {  // Empty entry
            newEntry = &dirEntries[i];
            break;
        }
    }

    if (!newEntry) {
        std::cerr << "Error: No space in directory to create new file.\n";
        return -1;
    }

    // Fill in the new file entry
    std::strncpy(newEntry->file_name, fileName.c_str(), sizeof(newEntry->file_name) - 1);
    newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate
    newEntry->first_blk = freeEntries[0];
    newEntry->size = totalSize;
    newEntry->type = TYPE_FILE;
    newEntry->access_rights = READ | WRITE;

    //split content into the required segment acording to BLOCK_SIZE
    size_t offset = 0;
    for (size_t i = 0; i < requiredBlocks; ++i) {
        uint8_t block[BLOCK_SIZE] = { 0 };
        size_t chunkSize = std::min(static_cast<size_t>(BLOCK_SIZE), totalSize - offset);
        std::memcpy(block, content.c_str() + offset, chunkSize);
        offset += chunkSize;
        writeBlock(freeEntries[i], block);
        //update fatetris
        if (i < requiredBlocks - 1) {
            fat[freeEntries[i]] = freeEntries[i + 1];
        } else {
            fat[freeEntries[i]] = FAT_EOF;
        }
    }

    // Update the FAT and the directory on the disk
    writeBlock(FAT_BLOCK, fat);
    writeBlock(dirBlk.first_blk, dirEntries);

    std::cout << "File '" << fileName << "' created successfully.\n";
    return 0;
}



// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath) {
    // Parse directory and file name from the given filepath
    size_t pos = filepath.find_last_of("/");
    std::string dirPath = filepath.substr(0, pos);
    std::string fileName = filepath.substr(pos + 1);

    // Load root directory block
    uint8_t dir[BLOCK_SIZE] = { 0 };
    readBlock(ROOT_BLOCK, dir);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(dir);

    // Find the file in the directory
    dir_entry fileEntry;
    bool fileFound = false;
    for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (std::strcmp(dirEntries[i].file_name, fileName.c_str()) == 0) {
            fileEntry = dirEntries[i];
            fileFound = true;
            break;
        }
    }
    if (!fileFound) {
        std::cerr << "Error: File not found.\n";
        return -1;
    }

    // Load FAT block
    uint8_t fatBlk[BLOCK_SIZE] = { 0 };
    readBlock(FAT_BLOCK, fatBlk);
    FATEntry* fatEntries = reinterpret_cast<FATEntry*>(fatBlk);

    // Read the file content from the disk

	uint8_t block[BLOCK_SIZE] = { 0 };
	disk.read(fileEntry.first_blk, block); //Read the first block.

	//This for-loop will start on the first block of the file and jump to the next block which the file is occupying in the FAT until it reaches FAT_EOF.
	for (int i = fileEntry.first_blk; i != EOF && i != 65535; i = fat[i])
	{
		disk.read(i, block);
		for (size_t i = 0; i < BLOCK_SIZE; i++)
			std::cout << block[i];
	}

    return 0;
}



// ls lists the content in the currect directory (files and sub-directories)
int FS::ls() {
    std::cout << "FS::ls()\n";
    
    // Read the current directory block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    std::cout << "Name\tSize\n";
    std::cout << "------------------------\n";
    
    // Iterate over directory entries
    for (auto i = 0u; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (dirEntries[i].file_name[0] != '\0') {  // Valid entry
            std::string entryName = dirEntries[i].file_name;
            auto entrySize = dirEntries[i].size;
            
            std::cout << entryName << "\t" << entrySize << " bytes\n";
        }
    }

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";
    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    std::cout << "FS::pwd()\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}
