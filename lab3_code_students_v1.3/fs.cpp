#include "fs.h"
#include <sstream>

#include <vector>
#include <string>

// Helper function to split path into components
std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> components;
    std::string delimiter = "/";
    size_t pos = 0;
    std::string token;
    std::string p = path;

    while ((pos = p.find(delimiter)) != std::string::npos) {
        token = p.substr(0, pos);
        if (!token.empty()) {
            components.push_back(token);
        }
        p.erase(0, pos + delimiter.length());
    }

    if (!p.empty()) {
        components.push_back(p);
    }

    return components;
}

//Helpers
// use block pointer to put the block in the path dir thets potentioly pointed to in path
// Resolve path to a directory block
int FS::resolvePath(const std::string& path) {
    uint8_t block[BLOCK_SIZE] = {0};
    std::vector<std::string> components = splitPath(path);
    uint8_t currentBlock = ROOT_BLOCK;
    dir_entry* dirEntries = nullptr;
    //if singel level, use current dir
    if (components.size() == 0) {
        return this->currentDir;
    }

    for (const std::string& component : components) {
        printf("component: %s\n", component.c_str());

        // Read the current directory block
        readBlock(currentBlock, block);
        dirEntries = reinterpret_cast<dir_entry*>(block);

        if (component == "..") {
            // Handle moving up one directory
            if (currentBlock == ROOT_BLOCK) {
                std::cerr << "Error: Already at root directory.\n";
                return ROOT_BLOCK;
            }

            bool parentFound = false;
            for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
                if (dirEntries[i].type == TYPE_DIR && strcmp(dirEntries[i].file_name, "..") == 0) {
                    currentBlock = dirEntries[i].first_blk;
                    parentFound = true;
                    break;
                }
            }
            if (!parentFound) {
                std::cerr << "Error: Parent directory not found.\n";
                return ROOT_BLOCK;
            }
        } else {
            // Find the directory entry
            bool found = false;
            for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
                if (strcmp(dirEntries[i].file_name, component.c_str()) == 0 && dirEntries[i].type == TYPE_DIR) {
                    currentBlock = dirEntries[i].first_blk;
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cerr << "Error: Directory not found.\n";
                return ROOT_BLOCK;
            }
        }
    }

    printf("currentBlock: %d\n", currentBlock);
    return currentBlock;
}





// Check if the entry has the required permissions
bool FS::hasPermission(const dir_entry& entry, uint8_t requiredRights) const {
    return (entry.access_rights & requiredRights) == requiredRights;
}

// Check if an entry is a file
bool FS::isFile(const dir_entry& entry) const {
    return entry.type == TYPE_FILE;
}

// Check if an entry is a directory
bool FS::isDirectory(const dir_entry& entry) const {
    return entry.type == TYPE_DIR;
}
// Check if the entry is valid
bool FS::isValidEntry(const dir_entry& entry) const {
    if (entry.file_name[0] == '\0') return false;
    if (entry.first_blk == 0 || entry.first_blk == 1) return false;
    if (std::strcmp(entry.file_name, ".") == 0 || std::strcmp(entry.file_name, "..") == 0) return false;
    return true;
}
std::string FS::accessRightsToString(uint8_t accessRights) const {
    std::string rights;
    rights += (accessRights & READ) ? 'r' : '-';
    rights += (accessRights & WRITE) ? 'w' : '-';
    rights += (accessRights & EXECUTE) ? 'x' : '-';
    return rights;
}
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
        writeBlock(freeEntries[i], (uint8_t*)block);
        //update fatetris
        if (i < requiredBlocks - 1) {
            fat[freeEntries[i]] = freeEntries[i + 1];
        } else {
            fat[freeEntries[i]] = FAT_EOF;
        }
    }

    // Update the FAT and the directory on the disk
    writeBlock(FAT_BLOCK, (uint8_t*)fat);
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
    format();
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
        writeBlock(i, (uint8_t*)initBlock);
    }

    uint8_t block[BLOCK_SIZE] = { 0 };
    dir_entry* root = (dir_entry*)block;

    std::string name(".");
    root[0].access_rights = READ | WRITE | EXECUTE;
    std::strncpy(root[0].file_name, name.c_str(), sizeof(root[0].file_name) - 1);
    root[0].file_name[sizeof(root[0].file_name) - 1] = '\0'; // Null-terminate
    root[0].first_blk = ROOT_BLOCK;
    root[0].size = 0; 
    root[0].type = TYPE_DIR; 

    // Parent directory entry ("..")
    name = "..";
    root[1].access_rights = READ | WRITE | EXECUTE;
    std::strncpy(root[1].file_name, name.c_str(), sizeof(root[1].file_name) - 1);
    root[1].file_name[sizeof(root[1].file_name) - 1] = '\0';
    root[1].first_blk = ROOT_BLOCK;
    root[1].size = 0; 
    root[1].type = TYPE_DIR; 
    
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
    // Find the current directory block
    std::string content = "";
    std::string line = "";
    size_t totalSize = 0;
    size_t requiredBlocks = 0;
    std::vector<FATEntry> freeEntries = {};
    dir_entry* newEntry = nullptr;
    size_t pos = filepath.find_last_of("/");
    std::string dirPath = filepath.substr(0, pos);  // Directory path
    std::string fileName = filepath.substr(pos + 1);  // File name

    if (fileName.size() > 55) {
        std::cerr << "Error: Invalid file name.\n";
        return -1;
    }
    uint8_t block[BLOCK_SIZE] = { 0 };
    dir_entry* dirEntries = nullptr;
    int blk = resolvePath(dirPath);
    readBlock(blk, block);
    dirEntries = reinterpret_cast<dir_entry*>(block);

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
    writeBlock(blk, (uint8_t*)dirEntries);
    return 0;
}
// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath) {
    dir_entry fileEntry;
    // Parse directory and file name from the given filepath
    size_t pos = filepath.find_last_of("/");
    std::string dirPath = filepath.substr(0, pos);  // Directory path
    std::string fileName = filepath.substr(pos + 1);  // File name
    uint8_t block[BLOCK_SIZE] = { 0 };
    dir_entry* dirEntries = nullptr;
    int blk = resolvePath(dirPath);
    readBlock(blk, block);
    dirEntries = reinterpret_cast<dir_entry*>(block);

    int index = findDirEntry(dirEntries, fileEntry, fileName);
    if (index == 0 || !isFile(fileEntry) || !hasPermission(fileEntry, READ)) {
        std::cerr << "Error: File not found or no read permission.\n";
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
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);
    std::cout << "Name\tType\taccessrights\tSize\n";
    
    for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i) {
        const dir_entry& entry = dirEntries[i];
        // exluded
        if (!isValidEntry(entry)) continue;
        //improve legebility
        std::string type = (entry.type == TYPE_DIR) ? "dir" : "file";
        std::string access = accessRightsToString(entry.access_rights);
        std::string bit = (type == "dir") ? "-" : std::to_string(entry.size) + " bytes";
        //print the shi
        std::cout << entry.file_name << "\t" << type << "\t\t" << access << "\t" << bit << "\n";
    }
    return 0;
}
int FS::cpDir(const std::string& sourcepath, const std::string& destpath, dir_entry* dir, uint16_t index) {
    // find sorce file
    dir_entry sourceEntry;
    uint16_t sourceIndex = findDirEntry(dir, sourceEntry, sourcepath);
    if (sourceIndex == 0 || !isDirectory(dir[index]) || !isDirectory(sourceEntry)) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    // check if file has rw permision
    if (hasPermission(sourceEntry, READ|WRITE) || hasPermission(dir[index], READ|WRITE)) {
        std::cerr << "Error: No read/write permission.\n";
        return -1;
    }
    
    //get the dest dir block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(dir[index].first_blk, block);
    dir_entry* destDir = reinterpret_cast<dir_entry*>(block);

    // find free space in the dest dir
    dir_entry *newEntry = nullptr;
    if (!createDirEntry(destDir, newEntry, sourceEntry.file_name)) {
        return -1;
    }
    // copy the file entry to the new dir
    std::strncpy(newEntry->file_name, sourceEntry.file_name, sizeof(newEntry->file_name) - 1);
    newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate
    newEntry->first_blk = sourceEntry.first_blk;
    newEntry->size = sourceEntry.size;
    newEntry->type = sourceEntry.type;
    newEntry->access_rights = sourceEntry.access_rights;    

    // write to disk for the dir table
    if((writeBlock(dir[index].first_blk, (uint8_t*)destDir) && writeBlock(this->currentDir, (uint8_t*)dir)) == 1) {
        return 0;
    }
    return -1;
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
    int dsindex = findDirEntry(dirEntries, dst, destpath);
    if (dsindex != 0) {
        if(dst.type != TYPE_DIR) {
            std::cerr << "Error: Destination file alredy exist.\n";
            return -1;
        }
        //copy file to dir
        return cpDir(sourcepath, destpath, dirEntries, dsindex);
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
    //rw permision form src file check
    if (hasPermission(sourceEntry, READ|WRITE)) {
        std::cerr << "Error: No read/write permission.\n";
        return -1;
    }

    // Fill in the new file entry
    std::strncpy(newEntry->file_name, destpath.c_str(), sizeof(newEntry->file_name) - 1);
    newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate
    newEntry->first_blk = freeEntries[0];
    newEntry->size = sourceEntry.size;
    newEntry->type = TYPE_FILE;
    newEntry->access_rights = sourceEntry.access_rights;    
    writePagesToFat(sourceEntry.size, file1Content, freeEntries);
    writeBlock(this->currentDir, (uint8_t*)dirEntries);
    return 0;
}

int FS::mvDir(const std::string& sourcepath, const std::string& destpath, dir_entry* dir, uint16_t index) {
    // find sorce file
    dir_entry sourceEntry;
    uint16_t sourceIndex = findDirEntry(dir, sourceEntry, sourcepath);
    if (sourceIndex == 0) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }

    if(!isFile(sourceEntry) || !isDirectory(dir[index]) || hasPermission(sourceEntry, READ|WRITE) || hasPermission(dir[index], READ|WRITE)) {
        std::cerr << "Error: TypeError.\n";
        return -1;    
    }
    //get the dest dir block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(dir[index].first_blk, block);
    dir_entry* destDir = reinterpret_cast<dir_entry*>(block);

    // find free space in the dest dir
    dir_entry *newEntry = nullptr;
    if (!createDirEntry(destDir, newEntry, sourceEntry.file_name)) {
        return -1;
    }
    // copy the file entry to the new dir
    std::strncpy(newEntry->file_name, sourceEntry.file_name, sizeof(newEntry->file_name) - 1);
    newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate
    newEntry->first_blk = sourceEntry.first_blk;
    newEntry->size = sourceEntry.size;
    newEntry->type = sourceEntry.type;
    newEntry->access_rights = sourceEntry.access_rights;    

    // write to disk for the dir table
    std::memset(&dir[sourceIndex], 0, sizeof(dir_entry));
    if((writeBlock(dir[index].first_blk, (uint8_t*)destDir) && writeBlock(this->currentDir, (uint8_t*)dir)) == 1) {
        return 0;
    }
    return -1;
}

int FS::mvFile(const std::string &sourcepath, const std::string &destpath, dir_entry* dir) {
    // Find the source file
    dir_entry sourceEntry;
    int dirEntryIndex = findDirEntry(dir, sourceEntry, sourcepath);
    if (dirEntryIndex == 0) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    // Check if the file is a file
    if (sourceEntry.type != TYPE_FILE) {
        std::cerr << "Error: Not a file.\n";
        return -1;
    }
    // Check if file has rw permision
    if ((sourceEntry.access_rights & READ) == 0 || (sourceEntry.access_rights & WRITE) == 0) {
        std::cerr << "Error: No read/write permission.\n";
        return -1;
    }
    // change name to dest filename
    std::strncpy(dir[dirEntryIndex].file_name, destpath.c_str(), sizeof(sourceEntry.file_name) - 1);
    // write to disk for the dir table
    writeBlock(this->currentDir, (uint8_t*)dir);
    return 0;
}
// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    // Find the current dirrectory table
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    //is detination a directory?
    dir_entry destEntry;
    uint16_t destIndex = findDirEntry(dirEntries, destEntry, destpath);
    if (isDirectory(destEntry)) {
        //move file to dir
        return mvDir(sourcepath, destpath, dirEntries, destIndex);
    } else {
        //move file to file
        if(destIndex != 0) {
            std::cerr << "Error: Destination file alredy exist.\n";
            return -1;
        }
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
    dir_entry sourceEntry;
    uint8_t fileEntry = findDirEntry(dirEntries, sourceEntry, filepath);
    if(!fileEntry) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    // Check if the file is a file
    if(!isFile(sourceEntry) || !hasPermission(sourceEntry, READ|WRITE)) {
        std::cerr << "Error.\n";
        return -1;
    }
    // find fat entries
    std::vector<FATEntry> fileEntries;
    for (auto i = dirEntries[fileEntry].first_blk; i != EOF && i != FAT_EOF; i = fat[i]) {
        fileEntries.push_back(i);
    }
    // clear the file content
    for (auto i = 0; i < fileEntries.size(); ++i) {
        uint8_t block[BLOCK_SIZE] = { 0 };
        writeBlock(fileEntries[i], (uint8_t*)block);
        fat[fileEntries[i]] = FAT_FREE;
    }
    // Step 5: Remove the directory entry
    std::memset(&dirEntries[fileEntry], 0, sizeof(dir_entry));

    // Step 6: Update the FAT and directory block on disk
    writeBlock(FAT_BLOCK, (uint8_t*)fat);
    writeBlock(this->currentDir, (uint8_t*)block);
    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2) {

    // Find the current directory block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    // Find the source file
    dir_entry sourceEntry;
    if (!findDirEntry(dirEntries, sourceEntry, filepath1)) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    // Read the source file content
    std::string content = "";
    uint8_t srcBlock[BLOCK_SIZE] = { 0 };
    for (auto i = sourceEntry.first_blk; i != EOF && i != FAT_EOF; i = fat[i]) {
        readBlock(i, srcBlock);
        content += reinterpret_cast<char*>(srcBlock);
    }
    // Find free FAT entries for the file
    std::vector<FATEntry> freeEntries = freeFATEntries(((content.length() + BLOCK_SIZE - 1) / BLOCK_SIZE));
    if (freeEntries.size() < ((content.length() + BLOCK_SIZE - 1) / BLOCK_SIZE)) {
        std::cerr << "Error: Not enough free blocks available.\n";
        return -1;
    }
    // find destination file, with id as well fore better write to memory
    dir_entry destEntry;
    uint16_t destIndex = findDirEntry(dirEntries, destEntry, filepath2);
    if (destIndex == 0) {
        // Destination file not found
        //create new dest file in current working dir, only have name and type
        dir_entry* newEntry = nullptr;
        if (!createDirEntry(dirEntries, newEntry, filepath2)) {
            std::cerr << "Error: Could not create new file entry.\n";
            return -1;
        }
        newEntry->first_blk = freeEntries[0];
        newEntry->size = 0;
        newEntry->type = TYPE_FILE;
        newEntry->access_rights = sourceEntry.access_rights;
        std::strncpy(newEntry->file_name, filepath2.c_str(), sizeof(newEntry->file_name) - 1);
        newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate

        // Write the updated current directory block to disk
        writeBlock(this->currentDir, (uint8_t*)block);
        // update the dicEntry list in memory
        readBlock(this->currentDir, block);
        dirEntries = reinterpret_cast<dir_entry*>(block);
        destIndex = findDirEntry(dirEntries, destEntry, filepath2);
    }
    // Check if the file is a file
    if(!isFile(sourceEntry) || hasPermission(sourceEntry, READ|WRITE)) {
        std::cerr << "Error.\n";
        return -1;
    }
    // recalkulate the size of the file
    dirEntries[destIndex].size += content.length();
    // write the pages to memory
    writePagesToFat(content.length(), content, freeEntries);
    //konekt the page chains
    if(freeEntries[0] == dirEntries[destIndex].first_blk) {
        // write to disk for the dir table
        writeBlock(this->currentDir, (uint8_t*)dirEntries);
        writeBlock(FAT_BLOCK, (uint8_t*)fat);
        return 0;
    }
    // find the last block in the file
    uint16_t lastBlock = dirEntries[destIndex].first_blk;
    while (fat[lastBlock] != FAT_EOF) {
        lastBlock = fat[lastBlock];
    }
    // connect the last block to the new blocks
    fat[lastBlock] = freeEntries[0];
    // write to disk for the dir table
    writeBlock(this->currentDir, (uint8_t*)dirEntries);
    writeBlock(FAT_BLOCK, (uint8_t*)fat);
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int 
FS::mkdir(std::string dirpath) {
    // Parse directory and file name from the given filepath
    size_t pos = dirpath.find_last_of("/");
    std::string dirName = dirpath.substr(pos + 1);
    uint8_t block[BLOCK_SIZE] = { 0 };
    uint8_t newBlock[BLOCK_SIZE] = { 0 };
    dir_entry* dirEntries = nullptr;
    dir_entry* newDir = nullptr;
    dir_entry* newDirEntries = reinterpret_cast<dir_entry*>(newBlock);

    // Read the current directory block
    readBlock(this->currentDir, block);
    dirEntries = reinterpret_cast<dir_entry*>(block);
    if (dirEntries == nullptr) {
        std::cerr << "Error: Could not read directory entries.\n";
        return -1;
    }

    // Check if the directory already exists
    dir_entry targetEntry;
    if (findDirEntry(dirEntries, targetEntry, dirName)) {
        std::cerr << "Error: Directory already exists.\n";
        return -1;
    }
    createDirEntry(dirEntries, newDir, dirName);
    // Find free disk space for the new directory
    std::vector<FATEntry> freeEntries = freeFATEntries(1);
    if (freeEntries.empty()) {
        std::cerr << "Error: Not enough free blocks available.\n";
        return -1;
    }
    //inherit acces right from current dir
    uint16_t access = dirEntries[0].access_rights;

    // Initialize the new directory entry in the current directory
    newDir->access_rights = access;
    std::strncpy(newDir->file_name, dirName.c_str(), sizeof(newDir->file_name) - 1);
    newDir->file_name[sizeof(newDir->file_name) - 1] = '\0'; // Null-terminate
    newDir->first_blk = freeEntries[0];
    newDir->size = 0; 
    newDir->type = TYPE_DIR;

    // Entry for "."
    std::strncpy(newDirEntries[0].file_name, ".", sizeof(newDirEntries[0].file_name) - 1);
    newDirEntries[0].file_name[sizeof(newDirEntries[0].file_name) - 1] = '\0';
    newDirEntries[0].first_blk = freeEntries[0];
    newDirEntries[0].size = 0;
    newDirEntries[0].type = TYPE_DIR;
    newDirEntries[0].access_rights = access;

    // Entry for ".."
    std::strncpy(newDirEntries[1].file_name, "..", sizeof(newDirEntries[1].file_name) - 1);
    newDirEntries[1].file_name[sizeof(newDirEntries[1].file_name) - 1] = '\0';
    newDirEntries[1].first_blk = this->currentDir;
    newDirEntries[1].size = 0;
    newDirEntries[1].type = TYPE_DIR;
    newDirEntries[1].access_rights = access;

    // Write the new directory block to disk
    disk.write(freeEntries[0], newBlock);

    // Update FAT and current directory
    fat[freeEntries[0]] = FAT_EOF;
    disk.write(FAT_BLOCK, (uint8_t*)fat);
    // Write the updated current directory block to disk
    writeBlock(this->currentDir, (uint8_t*)dirEntries);
    return 0;
}


// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    // Read the current directory block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    // Find the directory entry
    dir_entry targetEntry;
    if (!findDirEntry(dirEntries, targetEntry, dirpath)) {
        std::cerr << "Error: Directory not found.\n";
        return -1;
    }
    // Check if the entry is a directory and has read permission
    if(!isDirectory(targetEntry) || !hasPermission(targetEntry, READ)) {
        std::cerr << "Error.\n";
        return -1;
    }

    // Update current directory block number
    this->currentDir = targetEntry.first_blk;
    if (dirpath == "..") {
        this->currentPath.pop_back();
    } else {
        this->currentPath.push_back(targetEntry.file_name);
    }
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    std::string path = "/";
    for (auto &&i : this->currentPath)
    {
        path += i + "/";
    }
    std::cout << path << std::endl;
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
    // Find the current directory block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(this->currentDir, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    // Find the source file
    dir_entry sourceEntry;
    uint16_t fileIndex = findDirEntry(dirEntries, sourceEntry, filepath);
    if (!fileIndex) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    // Check if the file is a file
    if (sourceEntry.type != TYPE_FILE) {
        std::cerr << "Error: Not a file.\n";
        return -1;
    }
    //update the access rights
    uint8_t mask = 0;
    if(std::all_of(accessrights.begin(), accessrights.end(), ::isdigit)) {
        uint8_t num = std::stoi(accessrights);
        if (num < 0 || num > 7) {
            std::cerr << "Error: Invalid access rights.\n";
            return -1;
        }
        if (num & READ) mask |= READ;
        if (num & WRITE) mask |= WRITE;
        if (num & EXECUTE) mask |= EXECUTE;
        // XOR for mask (switch betwen access rights)
        //remove XOR for mask (switch betwen access rights) 
        dirEntries[fileIndex].access_rights = mask;      
        // Write the updated current directory block to disk
        writeBlock(this->currentDir, (uint8_t*)dirEntries);
    }
    else {
        std::cerr << "Error: Invalid access rights.\n";
        return -1;
    }
    return 0;
}
