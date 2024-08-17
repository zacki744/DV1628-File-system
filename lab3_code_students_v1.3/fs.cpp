#include "fs.h"


//Helpers
bool FS::createDirEntry(dir_entry* dirEntries, dir_entry*& newEntry, const std::string& fileName) {
    for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (dirEntries[i].file_name[0] == '\0') {  // Empty entry
            newEntry = &dirEntries[i];
            break;
        }
    }
    if (!newEntry) {
        std::cerr << "Error: No space in directory to create new file.\n";
        return false;
    }
    std::strncpy(newEntry->file_name, fileName.c_str(), sizeof(newEntry->file_name) - 1);
    newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate
    return true;
}
void FS::writePagesToFat(const size_t totalSize, const std::string content, const std::vector<FATEntry> freeEntries) {
    FATEntry requiredBlocks = freeEntries.size();
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
}
int FS::findDirEntry(dir_entry* dirTable, dir_entry& NewEntry, const std::string& name) {
    bool destFound = false;
    for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        if (std::strcmp(dirTable[i].file_name, name.c_str()) == 0) {
            NewEntry = dirTable[i];
            return i;
        }
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
        writeBlock(i, initBlock);
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
    std::string fileName = filepath.substr(pos + 1);
    std::string content = "";
    std::string line = "";
    size_t totalSize = 0;
    uint8_t block[BLOCK_SIZE] = { 0 };
    size_t requiredBlocks = 0;
    std::vector<FATEntry> freeEntries = {};
    dir_entry* newEntry = nullptr;

    if (fileName.size() > 55) {
        std::cerr << "Error: Invalid file name.\n";
        return -1;
    }
    // Check if the file already exists in the directory
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    // Capture the file content from user input
    while (true) {
        std::getline(std::cin, line);
        if (line.empty()) break;
        content += line + "\n";
        totalSize += line.length() + 1; // +1 for the newline character
    }
    // Calculate the number of blocks needed
    requiredBlocks = (totalSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
    // Find free FAT entries for the file
    freeEntries = freeFATEntries(requiredBlocks);
    if (freeEntries.size() < requiredBlocks) {
        std::cerr << "Error: Not enough free blocks available.\n";
        return -1;
    }
    // Create a new directory entry
    if (!createDirEntry(dirEntries, newEntry, fileName)) {
        return -1;
    }
    // Fill in the new file entry
    std::strncpy(newEntry->file_name, fileName.c_str(), sizeof(newEntry->file_name) - 1);
    newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate
    newEntry->first_blk = freeEntries[0];
    newEntry->size = totalSize;
    newEntry->type = TYPE_FILE;
    newEntry->access_rights = READ | WRITE;

    writePagesToFat(totalSize, content, freeEntries);
    writeBlock(this->currentDir, dirEntries);
    return 0;
}



// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath) {
    // Parse directory and file name from the given filepath
    size_t pos = filepath.find_last_of("/");
    std::string fileName = filepath.substr(pos + 1);

    uint8_t dir[BLOCK_SIZE] = { 0 };
    uint8_t block[BLOCK_SIZE] = { 0 };
    dir_entry fileEntry;
    dir_entry* dirEntries =nullptr;

    readBlock(this->currentDir, dir);
    dirEntries = reinterpret_cast<dir_entry*>(dir);

    // Find the file in the directory
    if(!findDirEntry(dirEntries, fileEntry, fileName)) {
        std::cerr << "Error: File not found.\n";
        return -1;
    }
    // Read the file content from the disk
	if(!readBlock(fileEntry.first_blk, block)) return -1; //Read the first block.permision to read the file
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
    // Read the current directory block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    std::cout << "Name\tSize\n";
    
    // Iterate over directory entries
    for (auto i = 0u; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        //check if valid entry i.e. not empty, and that its not fat or root
        if (dirEntries[i].file_name[0] != '\0' && dirEntries[i].first_blk != 0 && dirEntries[i].first_blk != 1) {
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
    uint8_t tmpBlk[BLOCK_SIZE] = { 0 };
    dir_entry sourceEntry = {};
    dir_entry dst = {};
    std::string file1Content = "";
    dir_entry* dirEntries = nullptr;
    dir_entry *newEntry = nullptr;

    readBlock(this->currentDir, block);
    dirEntries = reinterpret_cast<dir_entry*>(block);
    // Find the source file
    if(!findDirEntry(dirEntries, sourceEntry, sourcepath)) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    if(findDirEntry(dirEntries, dst, destpath)) {
        std::cerr << "Error: Destination file already exists.\n";
        return -1;
    }
    //get the required fatentries
    for (auto i = sourceEntry.first_blk; i != EOF && i != FAT_EOF; i = fat[i])
    {
        readBlock(i, tmpBlk);
        char* tmp = reinterpret_cast<char*>(tmpBlk);
        file1Content += tmp;
    }
    // Find free FAT entries for the file
    std::vector<FATEntry> freeEntries = freeFATEntries(((file1Content.length() + BLOCK_SIZE - 1) / BLOCK_SIZE));
    if (freeEntries.size() < ((file1Content.length() + BLOCK_SIZE - 1) / BLOCK_SIZE)) {
        std::cerr << "Error: Not enough free blocks available.\n";
        return -1;
    }
    // Create a new directory entry
    if (!createDirEntry(dirEntries, newEntry, destpath)) {
        std::cerr << "Error: Could not create new file entry.\n";
        return -1;
    }
    // Fill in the new file entry
    std::strncpy(newEntry->file_name, destpath.c_str(), sizeof(newEntry->file_name) - 1);
    newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate
    newEntry->first_blk = freeEntries[0];
    newEntry->size = sourceEntry.size;
    newEntry->type = TYPE_FILE;
    newEntry->access_rights = sourceEntry.access_rights;
    printf("file content: %s\n", file1Content.c_str());
    
    writePagesToFat(sourceEntry.size, file1Content, freeEntries);
    writeBlock(this->currentDir, dirEntries);
    return 0;
}

int FS::mvDir(const std::string& sourcepath, const std::string& destpath, dir_entry* dir, dir_entry* destEntry) {
    // find sorce file
    dir_entry *sourceEntry;
    if(!findDirEntry(dir, *sourceEntry, sourcepath)) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    
    //get the dest dir block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(destEntry->first_blk, block);
    dir_entry* destDir = reinterpret_cast<dir_entry*>(block);

    // find free space in the dest dir
    dir_entry *newEntry = nullptr;
    if (!createDirEntry(destDir, newEntry, sourceEntry->file_name)) {
        return -1;
    }
    // copy the file entry to the new dir
    std::strncpy(newEntry->file_name, sourceEntry->file_name, sizeof(newEntry->file_name) - 1);
    newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate
    newEntry->first_blk = sourceEntry->first_blk;
    newEntry->size = sourceEntry->size;
    newEntry->type = sourceEntry->type;
    newEntry->access_rights = sourceEntry->access_rights;    

    // write to disk for the dir table
    if((writeBlock(destEntry->first_blk, destDir) && writeBlock(this->currentDir, dir)) == 1)
            std::memset(sourceEntry, 0, sizeof(dir_entry));

     // remove the file dir from the old destination
    return 0;
}

int FS::mvFile(const std::string &sourcepath, const std::string &destpath, dir_entry* dir) {
    // Find the source file
    dir_entry sourceEntry;
    int dirEntryIndex = findDirEntry(dir, sourceEntry, sourcepath);
    std::cout << "Source file found\n";
    std::cout << "Source file name: " << dir[dirEntryIndex].file_name << std::endl;
    std::cout << "destpath: " << destpath << std::endl;
    // change name to dest filename
    std::strncpy(dir[dirEntryIndex].file_name, destpath.c_str(), sizeof(sourceEntry.file_name) - 1);
    // write to disk for the dir table
    std::cout << "Writing to disk\n";
    std::cout << "New Source file name: " << sourceEntry.file_name << std::endl;
    writeBlock(this->currentDir, dir);
    return 0;
}
// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
    ls();
    // Find the current dirrectory table
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    //is detination a directory?
    dir_entry destEntry;
    bool dir = findDirEntry(dirEntries, destEntry, destpath);
    if (dir) {
        //move file to directory
        std::cout << "Moving file to directory\n";
        return mvDir(sourcepath, destpath, dirEntries, &destEntry);
    } else {
        //move file to file
        std::cout << "Moving file to file\n";
        return mvFile(sourcepath, destpath, dirEntries);
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
