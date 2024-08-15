#include "fs.h"


//Helpers
bool FS::findDirEntry(dir_entry* dirTable, dir_entry& NewEntry, const std::string& name) {
    bool destFound = false;
    for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (std::strcmp(dirTable[i].file_name, name.c_str()) == 0) {
            NewEntry = dirTable[i];
            destFound = true;
            return 1;
        }
    }
    if (!destFound) {
        std::cerr << "Error: Directory not found.\n";
        return 0;
    }
    return 0;
}

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
            freeEntries.push_back(i);
            if (freeEntries.size() == size) {
                break;
            }
        }
    }
    return freeEntries;
}
dir_entry FS::find_dir_block(const std::string& filepath) {//idk meby remove... forgot that i had this?
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
    for (auto i = 0; i < disk.get_no_blocks(); i++) {
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

    // Check if the file already exists in the directory
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

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
    for (auto i = 0; i < requiredBlocks; ++i) {
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
    writeBlock(this->currentDir, dirEntries);

    std::cout << "File '" << fileName << "' created successfully.\n";
    return 0;
}



// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath) {
    // Parse directory and file name from the given filepath
    size_t pos = filepath.find_last_of("/");
    std::string fileName = filepath.substr(pos + 1);
    // Locate the directory block where the file should be created
    uint8_t dir[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, dir);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(dir);

    // Find the file in the directory
    dir_entry fileEntry;
    if(!findDirEntry(dirEntries, fileEntry, fileName)) {
        std::cerr << "Error: File not found.\n";
        return -1;
    }

    // Load FAT block
    uint8_t fatBlk[BLOCK_SIZE] = { 0 };
    readBlock(FAT_BLOCK, fatBlk);
    FATEntry* fatEntries = reinterpret_cast<FATEntry*>(fatBlk);

    // Read the file content from the disk

	uint8_t block[BLOCK_SIZE] = { 0 };
	disk.read(fileEntry.first_blk, block); //Read the first block.permision to read the file

    //duble check permision to read the file
    if ((fileEntry.access_rights & READ) == 0) {
        std::cerr << "Error: No read permission.\n";
        return -1;
    }

	//This for-loop will start on the first block of the file and jump to the next block which the file is occupying in the FAT until it reaches FAT_EOF.
	for (auto i = fileEntry.first_blk; i != EOF && i != FAT_EOF; i = fat[i])
	{
		disk.read(i, block);
		for (auto i = 0; i < BLOCK_SIZE; i++)
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
FS::cp(std::string sourcepath, std::string destpath) //currently only working in one directory (working dirrectory)
{
    if (sourcepath == destpath){
        std::cerr << "Error: Source and destination are the same.\n";
        return -1;
    }
    // Find the current dirrectory table'
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    // Find the source file
    dir_entry *sourceEntry;
    if(!findDirEntry(dirEntries, *sourceEntry, sourcepath)) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    //get the required fatentries
    std::vector<FATEntry> file1files;
    FATEntry j = sourceEntry->first_blk;
    for (auto i = sourceEntry->first_blk; i != EOF && i != FAT_EOF; i = fat[i]) {
        file1files.push_back(i);
    }
    // Find free FAT entries for the file
    std::vector<FATEntry> freeEntries = freeFATEntries(file1files.size());
    if (freeEntries.size() < file1files.size()) {
        std::cerr << "Error: Not enough free blocks available.\n";
        return -1;
    }
    // Create a new directory entry
    dir_entry *newEntry = nullptr;
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
    std::strncpy(newEntry->file_name, destpath.c_str(), sizeof(newEntry->file_name) - 1);
    newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate
    newEntry->first_blk = freeEntries[0];
    newEntry->size = sourceEntry->size;
    newEntry->type = TYPE_FILE;
    newEntry->access_rights = sourceEntry->access_rights;

    // Copy the file content
    for (auto i = 0; i < file1files.size() - 1; ++i) {
        uint8_t block[BLOCK_SIZE] = { 0 };
        readBlock(j, block);
        writeBlock(freeEntries[i], block);
        if (i < file1files.size() - 1) {
            fat[freeEntries[i]] = freeEntries[i + 1];
        } else {
            fat[freeEntries[i]] = FAT_EOF;
        }
        j = fat[j];
    }
    // Update the FAT and the directory on the disk
    writeBlock(FAT_BLOCK, fat);
    writeBlock(this->currentDir, dirEntries);
    return 0;
}

int FS::mv_dir(const std::string& sourcepath, const std::string& destpath, dir_entry* dir) {
    // find sorce file
    dir_entry *sourceEntry;
    bool sourceFound = false;
    if(!findDirEntry(dir, *sourceEntry, sourcepath)) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }

    //find dest dir
    dir_entry *destTable;
    bool destFound = false;
    if(!findDirEntry(dir, *destTable, destpath)) { //single level serch. shuld be expanded to serch more then one layer of dir 
        std::cerr << "Error: Destination directory not found.\n";
        return -1;
    }
    //get the dest dir block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(destTable->first_blk, block);
    dir_entry* destDir = reinterpret_cast<dir_entry*>(block);

    // find free space in the dest dir
    dir_entry *newEntry = nullptr;
    for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (destDir[i].file_name[0] == '\0') {  // Empty entry
            newEntry = &destDir[i];
            break;
        }
    }
    if (!newEntry) {
        std::cerr << "Error: No space in directory to move file.\n";
        return -1;
    }
    // copy the file entry to the new dir
    std::strncpy(newEntry->file_name, sourceEntry->file_name, sizeof(newEntry->file_name) - 1);
    newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate
    newEntry->first_blk = sourceEntry->first_blk;
    newEntry->size = sourceEntry->size;
    newEntry->type = sourceEntry->type;
    newEntry->access_rights = sourceEntry->access_rights;    

    // remove the file dir from the old destination
    std::memset(sourceEntry, 0, sizeof(dir_entry));
    // write to disk for the dir table
    writeBlock(destTable->first_blk, destDir);
    writeBlock(this->currentDir, dir);
    return 0;
}

int FS::mv_file(const std::string &sourcepath, const std::string &destpath, dir_entry* dir) {
    // Find the source file
    dir_entry *sourceEntry;
    bool sourceFound = false;
    if(!findDirEntry(dir, *sourceEntry, sourcepath)) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    // change name to dest filename
    std::strncpy(sourceEntry->file_name, destpath.c_str(), sizeof(sourceEntry->file_name) - 1);
    // write to disk for the dir table
    writeBlock(this->currentDir, dir);
    return 0;
}
// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
    // Find the current dirrectory table
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    //is detination a directory?
    bool isDir = false;
    for (auto i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (std::strcmp(dirEntries[i].file_name, destpath.c_str()) == 0) {
            isDir = dirEntries[i].type == TYPE_DIR;
            break;
        }
    }
    if(isDir){
        return mv_dir(sourcepath, destpath, dirEntries);
        
    }else{
       return mv_file(sourcepath, destpath, dirEntries);
    }
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";
    // Find the current dirrectory table
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    // Find the source file
    dir_entry *sourceEntry;
    if(!findDirEntry(dirEntries, *sourceEntry, filepath)) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    // clear file content
    FATEntry j = sourceEntry->first_blk;
    for (size_t i = 0; i < sourceEntry->size / BLOCK_SIZE; ++i) {
        fat[j] = FAT_FREE;
        j = fat[j];
    }
    // remove the file dir from the old destination
    std::memset(sourceEntry, 0, BLOCK_SIZE);
    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";
    // Find the current dirrectory table
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    // Find the File 1
    dir_entry *File1;
    if(!findDirEntry(dirEntries, *File1, filepath1)) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    // Find file 2
    dir_entry *File2;
    if(!findDirEntry(dirEntries, *File2, filepath2)) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    // Check if the file is a file
    if (File1->type != TYPE_FILE || File2->type != TYPE_FILE) {
        std::cerr << "Error: Not a file.\n";
        return -1;
    }
    // Check if file 1 has rw and file 2 has r permision
    if ((File2->access_rights & READ) == 0 || (File2->access_rights & WRITE) == 0 || (File1->access_rights & READ) == 0) {
        std::cerr << "Error: No read/write permission.\n";
        return -1;
    }

    // Find and copy file 1 to temp variables, then determine the size of the new file
    std::vector<FATEntry> file1files;
    FATEntry j = File1->first_blk;
    for (auto i = File1->first_blk; i != EOF && i != FAT_EOF; i = fat[i]) {
        file1files.push_back(i);
    }
    // Find free FAT entries for the file
    std::vector<FATEntry> freeEntries = freeFATEntries(file1files.size());
    if (freeEntries.size() < file1files.size()) {
        std::cerr << "Error: Not enough free blocks available.\n";
        return -1;
    }   
    // Copy the file content
    for (size_t i = 0; i < file1files.size(); ++i) {
        uint8_t block[BLOCK_SIZE] = { 0 };
        readBlock(j, block);
        writeBlock(freeEntries[i], block);
        if (i < file1files.size() - 1) {
            fat[freeEntries[i]] = freeEntries[i + 1];
        } else {
            fat[freeEntries[i]] = FAT_EOF;
        }
        j = fat[j];
    }
    // Update last entry in file 2 to point to the first copied entrys
    FATEntry file2Walker = File2->first_blk;
    for (auto i = j; i != EOF && i != FAT_EOF; i = fat[i]) {
        file2Walker = i;
    }
    fat[file2Walker] = freeEntries[0];
    // Update the FAT and the directory on the disk
    writeBlock(FAT_BLOCK, fat);
    writeBlock(this->currentDir, dirEntries);
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
    // Check if the destination is the root directory
    if (dirpath == "/") {
        this->currentDir = ROOT_BLOCK;
        return 0;
    }
    // Check if the destination is the parent directory
    if (dirpath == "..") {
        uint8_t block[BLOCK_SIZE] = { 0 };
        readBlock(this->currentDir, block);
        dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);
        // Find the parent directory
        dir_entry parentEntry;
        if (!findDirEntry(dirEntries, parentEntry, "..")) {
            return -1;
        }
        // Update the current directory
        this->currentDir = parentEntry.first_blk;
        return 0;
    }
    // check for multi line path
    if (dirpath.find("/") != std::string::npos) {
        std::cerr << "Error: Multi-level path not supported.\n";
        return -1;
    }

    //get the current directory
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);
    // Find destination directory
    dir_entry destEntry;
    if (!findDirEntry(dirEntries, destEntry, dirpath)) {
        return -1;
    }
    // Check if the destination is a directory
    if (destEntry.type != TYPE_DIR) {
        std::cerr << "Error: Not a directory.\n";
        return -1;
    }
    // Update the current directory
    this->currentDir = destEntry.first_blk;
    // update the current directory
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
