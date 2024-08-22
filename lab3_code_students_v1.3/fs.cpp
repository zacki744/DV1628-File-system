#include "fs.h"
#include <sstream>
#include <vector>
#include <string>

// Helper function to split path into components
std::vector<std::string> FS::splitPath(const std::string& path) {
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
    dir_entry destEntry;
    std::vector<std::string> components = splitPath(path);
    uint8_t currentBlock = this->currentDir;
    dir_entry* dirEntries = nullptr;
    //if singel level, use current dir
    if (components.size() == 0) {
        return this->currentDir;
    }
    for (const std::string& component : components) {
        uint8_t block[BLOCK_SIZE] = {0};
        readBlock(currentBlock, block);
        dirEntries = reinterpret_cast<dir_entry*>(block);
        if (component == "..") {
            // Handle moving up one directory
            if (currentBlock == ROOT_BLOCK) {
                std::cerr << "Error: Already at root directory.\n";
                return currentBlock;
            }
            currentBlock = dirEntries[1].first_blk;
        } else {
            // Find the directory entry
            int dirEntryIndex = findDirEntry(dirEntries, destEntry, component);
            if (!dirEntryIndex) {
                // Directory not found
                std::cerr << "Error: Directory not found.\n";
                return this->currentDir;
            }
            if(!isDirectory(destEntry) || !hasPermission(destEntry, READ|EXECUTE)) { //end point for walker, we found a file, cant navigate to a file as a directory... smh
                return currentBlock;
            }
            currentBlock = dirEntries[dirEntryIndex].first_blk;
        }
    }
    return currentBlock;
}

// Check if the entry has the required permissions
bool FS::hasPermission(const dir_entry& entry, uint8_t requiredRights) const {
    //acces rights are stored as a bitfield, 0x04 = read, 0x02 = write, 0x01 = execute
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
    disk.~Disk();
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
    uint8_t block[BLOCK_SIZE] = { 0 };
    dir_entry* dirEntries = nullptr;
    std::string fileName;
    int blk = currentDir;
    if(pos != std::string::npos) {
        std::string dirPath = filepath.substr(0, pos);  // Directory path
        fileName = filepath.substr(pos + 1);  // File name
        blk = resolvePath(dirPath);
        readBlock(blk, block);
        dirEntries = reinterpret_cast<dir_entry*>(block);
    }
    else {
        fileName = filepath;
        readBlock(this->currentDir, block);
        dirEntries = reinterpret_cast<dir_entry*>(block);
    }


    if (fileName.size() > 55) {
        std::cerr << "Error: Invalid file name.\n";
        return -1;
    }

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
    uint8_t srcBlk[BLOCK_SIZE] = { 0 };
    uint8_t tmpBlk[BLOCK_SIZE] = { 0 };
    // entris
    dir_entry* dirEntries = nullptr;
    dir_entry* destDirEntries = nullptr;
    dir_entry* newEntry = nullptr;
    // source
    dir_entry sourceEntry;
    // dest
    dir_entry destEntry;
    // find the source path
    uint16_t srcIndex = 0;
    uint16_t dsindex = 0;
    std::string file1Content = "";
    size_t pos = sourcepath.find_last_of("/");
    std::string fileName;
    FATEntry blk = this->currentDir;
    FATEntry dsblk = this->currentDir;
    if(pos != std::string::npos) {
        std::string dirPath = sourcepath.substr(0, pos);  // Directory path
        fileName = sourcepath.substr(pos + 1);  // File name
        blk = resolvePath(dirPath);
    }
    else {
        fileName = sourcepath;
    }    
    //resolve the path dst
    pos = destpath.find_last_of("/");
    std::string destDirPath;
    if(pos != std::string::npos) {
        destDirPath = destpath.substr(0, pos);  // Directory path
        destpath = destpath.substr(pos + 1);  // File name
        dsblk = resolvePath(destDirPath);
    }
    else {
        destDirPath = destpath;
    }
    //read the dir block
    readBlock(blk, srcBlk);
    dirEntries = reinterpret_cast<dir_entry*>(srcBlk);
    //read the dest dir block
    readBlock(dsblk, block);
    destDirEntries = reinterpret_cast<dir_entry*>(block);
    // find the source file
    srcIndex = findDirEntry(dirEntries, sourceEntry, fileName);
    dsindex = findDirEntry(destDirEntries, destEntry, destpath);


    if (srcIndex == 0) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    if (dsindex != 0) {
        if(!isDirectory(destEntry)) {
            std::cerr << "Error: dst not a dir.\n";
            return -1;
        }
        //rread new dst block for the dir
        uint8_t block[BLOCK_SIZE] = { 0 };
        readBlock(destDirEntries[dsindex].first_blk, block);
        destDirEntries = reinterpret_cast<dir_entry*>(block);
        destpath = sourceEntry.file_name;
    }
    for (auto i = sourceEntry.first_blk; i != EOF && i != FAT_EOF; i = fat[i])
    {
        readBlock(i, tmpBlk);
        char* tmp = reinterpret_cast<char*>(tmpBlk);
        file1Content += tmp;
    }
    // Find free FAT entries for the file
    if (file1Content.length() == 0) {
        std::cerr << "Error: Source file is empty.\n";
        return -1;
    }
    std::vector<FATEntry> freeEntries = freeFATEntries(((file1Content.length() + BLOCK_SIZE - 1) / BLOCK_SIZE));
    if (freeEntries.size() < ((file1Content.length() + BLOCK_SIZE - 1) / BLOCK_SIZE)) {
        std::cerr << "Error: Not enough free blocks available.\n";
        return -1;
    }
    // Create a new directory entry
    if (!createDirEntry(destDirEntries, newEntry, destpath)) {
        std::cerr << "Error: Could not create new file entry.\n";
        return -1;
    }
    //rw permision form src file check
    if (!hasPermission(sourceEntry, READ)) {
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
    writeBlock(destDirEntries[0].first_blk, (uint8_t*)destDirEntries);
    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    uint8_t srcBlock[BLOCK_SIZE] = { 0 };
    uint8_t dstBlock[BLOCK_SIZE] = { 0 };
    dir_entry* srcDirEntris = nullptr;
    dir_entry* destEntries = nullptr;
    dir_entry* newEntry = nullptr;
    dir_entry source;
    dir_entry dest;
    uint16_t srcIndex = 0;
    uint16_t dstIndex = 0;
    size_t pos = sourcepath.find_last_of("/");
    std::string fileName;
    FATEntry blk = this->currentDir;
    FATEntry dsblk = this->currentDir;
    if(pos != std::string::npos) {
        std::string dirPath = sourcepath.substr(0, pos);  // Directory path
        fileName = sourcepath.substr(pos + 1);  // File name
        blk = resolvePath(dirPath);
    }
    else {
        fileName = sourcepath;
    }
    //resolve the path dst
    pos = destpath.find_last_of("/");
    std::string destDirPath;
    if(pos != std::string::npos) {
        destDirPath = destpath.substr(0, pos);  // Directory path
        destpath = destpath.substr(pos + 1);  // File name
        dsblk = resolvePath(destDirPath);
    }
    else {
        destDirPath = destpath;
    }
    readBlock(blk, srcBlock);
    srcDirEntris = reinterpret_cast<dir_entry*>(srcBlock);
    readBlock(dsblk, dstBlock);
    destEntries = reinterpret_cast<dir_entry*>(dstBlock);
    srcIndex = findDirEntry(srcDirEntris, source, fileName);
    dstIndex = findDirEntry(destEntries, dest, destpath);
    if(srcIndex == 0) {
        std::cerr << "Error: not found.\n";
        return -1;
    }
    if(isDirectory(source)) {
        std::cerr << "Error: Source is a directory\n";
        return -1;
    }
    if (!hasPermission(source, READ)) {
        std::cerr << "Error: No read permission.\n";
        return -1;
    }
    // two routs, one for copy to file and the other for copy to dir
    if(dstIndex != 0) {
        if(isDirectory(dest)) {
            //copy to dir
            uint8_t block[BLOCK_SIZE] = { 0 };
            readBlock(destEntries[dstIndex].first_blk, block);
            destEntries = reinterpret_cast<dir_entry*>(block);
            destpath = source.file_name;
            if (!hasPermission(dest, WRITE)) {
                std::cerr << "Error: No write permission.\n";
                return -1;
            }
            if (findDirEntry(destEntries, dest, destpath)) { //reuse dest as it wont be used any more
                std::cerr << "Error: Destination file already exists.\n";
                return -1;
            }
            if (!createDirEntry(destEntries, newEntry, destpath)) {
                std::cerr << "Error: Could not create new file entry.\n";
                return -1;
            }
            std::memcpy(newEntry, &srcDirEntris[srcIndex], sizeof(dir_entry));
            writeBlock(destEntries[0].first_blk, (uint8_t*)destEntries);
            std::memset(&srcDirEntris[srcIndex], 0, sizeof(dir_entry));
            writeBlock(blk, (uint8_t*)srcDirEntris);
            return 0;
        }
        else {
            std::cerr << "Error: Destination is not a directory.\n";
            return -1;
        }
    }
    //copy to file
    if (!createDirEntry(destEntries, newEntry, destpath)) {
        std::cerr << "Error: Could not create new file entry.\n";
        return -1;
    }
    if (blk == dsblk) {
        // same dir
        //update the file name only, on the file on src
        std::strncpy(srcDirEntris[srcIndex].file_name, destpath.c_str(), sizeof(srcDirEntris[srcIndex].file_name) - 1);
        srcDirEntris[srcIndex].file_name[sizeof(srcDirEntris[srcIndex].file_name) - 1] = '\0'; // Null-terminate
        writeBlock(blk, (uint8_t*)srcDirEntris);
        return 0;
    }
    // basicly just change the name and dir position if src and dst hapend to be in diffrent dirs (persumend)
    std::memcpy(newEntry, &srcDirEntris[srcIndex], sizeof(dir_entry));
    std::strncpy(newEntry->file_name, destpath.c_str(), sizeof(newEntry->file_name) - 1);
    newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate
    writeBlock(dsblk, (uint8_t*)destEntries);
    std::memset(&srcDirEntris[srcIndex], 0, sizeof(dir_entry));
    writeBlock(blk, (uint8_t*)srcDirEntris);
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";

    // Extracts directory both path and file name from filepath
    size_t pos = filepath.find_last_of('/');
    std::string dirPath = (pos == std::string::npos) ? "" : filepath.substr(0, pos);
    std::string fileName = (pos == std::string::npos) ? filepath : filepath.substr(pos + 1);

    // Resolve the directory path to get the block number
    uint8_t parentDirBlock = resolvePath(dirPath);
    if (parentDirBlock == FAT_EOF) {
        std::cerr << "Error: Directory not found.\n";
        return -1;
    }

    // Reads the parent directory block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(parentDirBlock, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    // Finds the file entry in the directory
    dir_entry sourceEntry;
    uint8_t fileEntry = findDirEntry(dirEntries, sourceEntry, fileName);
    if (fileEntry == -1) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }

    // Checks if the entry is a file and has the right permissions
    if (!isFile(sourceEntry) || !hasPermission(sourceEntry, READ | WRITE)) {
        std::cerr << "Error: Not a file or insufficient permissions.\n";
        return -1;
    }

    // All FAT entries related to the file is collected
    std::vector<FATEntry> fileEntries;
    for (auto i = dirEntries[fileEntry].first_blk; i != FAT_EOF && i != FAT_FREE; i = fat[i]) {
        fileEntries.push_back(i);
    }

    // Clear file content and free FAT entries
    for (auto& blk : fileEntries) {
        uint8_t emptyBlock[BLOCK_SIZE] = { 0 };
        writeBlock(blk, emptyBlock);
        fat[blk] = FAT_FREE;
    }
// Remove the directory entry
    std::memset(&dirEntries[fileEntry], 0, sizeof(dir_entry));

    // Update the FAT and directory block on disk
    writeBlock(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
    writeBlock(parentDirBlock, block);

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2) {

    size_t pos1 = filepath1.find_last_of("/");
    std::string name1 = filepath1.substr(pos1 + 1);
    std::string dirName1 = (pos1 != std::string::npos) ? filepath1.substr(0, pos1) : "";
    size_t pos2 = filepath2.find_last_of("/");
    std::string name2 = filepath2.substr(pos2 + 1);
    std::string dirName2 = (pos2 != std::string::npos) ? filepath2.substr(0, pos2) : "";

    uint8_t blk1 = (dirName1.empty()) ? this->currentDir : resolvePath(dirName1);
    uint8_t blk2 = (dirName2.empty()) ? this->currentDir : resolvePath(dirName2);
    if ((blk1 == FAT_EOF) || (blk2 == FAT_EOF)) {
        std::cerr << "Error: Directory not found.\n";
        return -1;
    }

    // Read the parent directory block
    uint8_t block1[BLOCK_SIZE] = { 0 };
    uint8_t block2[BLOCK_SIZE] = { 0 };
    readBlock(blk1, block1);
    readBlock(blk2, block2);
    dir_entry* dirEntries1 = reinterpret_cast<dir_entry*>(block1);
    dir_entry* dirEntries2 = reinterpret_cast<dir_entry*>(block2);
    if ((dirEntries1 == nullptr) || (dirEntries2 == nullptr)) {
        std::cerr << "Error: Could not read directory entries.\n";
        return -1;
    }

    // Find the source file
    dir_entry sourceEntry;
    if (!findDirEntry(dirEntries1, sourceEntry, name1)) {
        std::cerr << "Error: Source file not found.\n";
        return -1;
    }
    // Read the source file content
    std::string content = "";
    uint8_t srcBlock[BLOCK_SIZE] = { 0 };
    size_t totalSize = 0;
    for (auto i = sourceEntry.first_blk; i != EOF && i != FAT_EOF; i = fat[i]) {
        readBlock(i, srcBlock);
        std::string line = reinterpret_cast<char*>(srcBlock);
        content += line;
        totalSize += line.length() + 1; // +1 for the newline character
    }
    // Find free FAT entries for the file
    std::vector<FATEntry> freeEntries = freeFATEntries(((content.length() + BLOCK_SIZE - 1) / BLOCK_SIZE));
    if (freeEntries.size() < ((content.length() + BLOCK_SIZE - 1) / BLOCK_SIZE)) {
        std::cerr << "Error: Not enough free blocks available.\n";
        return -1;
    }
    // find destination file, with id as well fore better write to memory
    dir_entry destEntry;
    uint16_t destIndex = findDirEntry(dirEntries2, destEntry, name2);
    if (destIndex == 0) {
        // Destination file not found
        //create new dest file in current working dir, only have name and type
        dir_entry* newEntry = nullptr;
        if (!createDirEntry(dirEntries2, newEntry, name2)) {
            std::cerr << "Error: Could not create new file entry.\n";
            return -1;
        }
        newEntry->first_blk = freeEntries[0];
        newEntry->size = totalSize;
        newEntry->type = TYPE_FILE;
        newEntry->access_rights = sourceEntry.access_rights;
        std::strncpy(newEntry->file_name, name2.c_str(), sizeof(newEntry->file_name) - 1);
        newEntry->file_name[sizeof(newEntry->file_name) - 1] = '\0'; // Null-terminate

        // Write the updated current directory block to disk
        writeBlock(dirEntries2[0].first_blk, (uint8_t*)block2);
        // update the dicEntry list in memory
        readBlock(dirEntries2[0].first_blk, block2);
        dirEntries2 = reinterpret_cast<dir_entry*>(block2);
        destIndex = findDirEntry(dirEntries2, destEntry, name2);
        writePagesToFat(content.length(), content, freeEntries);
        writeBlock(dirEntries2[0].first_blk, (uint8_t*)dirEntries2);
        writeBlock(FAT_BLOCK, (uint8_t*)fat);
        return 0;
    }
    if(!isFile(sourceEntry) || !hasPermission(sourceEntry, READ) || !hasPermission(destEntry, WRITE)) {
        std::cerr << "Error: type/permision.\n";
        return -1;
    }
    // recalkulate the size of the file
    dirEntries2[destIndex].size += totalSize;
    // write the pages to memory
    writePagesToFat(content.length(), content, freeEntries);
    uint16_t lastBlock = dirEntries2[destIndex].first_blk;
    while (fat[lastBlock] != FAT_EOF) {
        lastBlock = fat[lastBlock];
    }
    // connect the last block to the new blocks
    fat[lastBlock] = freeEntries[0];
    // write to disk for the dir table
    writeBlock(dirEntries2[0].first_blk, (uint8_t*)dirEntries2);
    writeBlock(FAT_BLOCK, (uint8_t*)fat);
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory 
int FS::mkdir(std::string dirpath) {
    // Parse directory and file name from the given filepath
    size_t pos = dirpath.find_last_of("/");
    std::string dirName = dirpath.substr(pos + 1);
    std::string parentDirPath = (pos != std::string::npos) ? dirpath.substr(0, pos) : "";

    // Resolve the path to the parent directory
    uint8_t parentDirBlock = (parentDirPath.empty()) ? this->currentDir : resolvePath(parentDirPath);
    if (parentDirBlock == FAT_EOF) {
        std::cerr << "Error: Directory not found.\n";
        return -1;
    }

    // Read the parent directory block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(parentDirBlock, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);
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

    // Create a new directory entry in the parent directory
    dir_entry* newDir = nullptr;
    if (!createDirEntry(dirEntries, newDir, dirName)) {
        return -1;
    }

    // Find free disk space for the new directory
    std::vector<FATEntry> freeEntries = freeFATEntries(1);
    if (freeEntries.empty()) {
        std::cerr << "Error: Not enough free blocks available.\n";
        return -1;
    }
    uint16_t access = dirEntries[0].access_rights;

    // Initialize the new directory entry in the parent directory
    newDir->access_rights = access;
    std::strncpy(newDir->file_name, dirName.c_str(), sizeof(newDir->file_name) - 1);
    newDir->file_name[sizeof(newDir->file_name) - 1] = '\0'; // Null-terminate
    newDir->first_blk = freeEntries[0];
    newDir->size = 0; 
    newDir->type = TYPE_DIR;

    // Prepare the new directory block (initialize with "." and "..")
    uint8_t newBlock[BLOCK_SIZE] = { 0 };
    dir_entry* newDirEntries = reinterpret_cast<dir_entry*>(newBlock);

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
    newDirEntries[1].first_blk = parentDirBlock;
    newDirEntries[1].size = 0;
    newDirEntries[1].type = TYPE_DIR;
    newDirEntries[1].access_rights = access;

    // Write the new directory block to disk
    writeBlock(freeEntries[0], newBlock);

    // Update FAT and the parent directory
    fat[freeEntries[0]] = FAT_EOF;
    writeBlock(FAT_BLOCK, (uint8_t*)fat);

    // Write the updated parent directory block to disk
    writeBlock(parentDirBlock, (uint8_t*)dirEntries);

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    // Read the current directory block
    uint8_t currblk[BLOCK_SIZE] = { 0 };
    uint8_t dirblk[BLOCK_SIZE] = { 0 };
    int blk = resolvePath(dirpath);
    readBlock(blk, currblk);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(currblk);
    readBlock(dirEntries[0].first_blk, dirblk);
    dirEntries = reinterpret_cast<dir_entry*>(dirblk);

    std::vector<std::string> path = splitPath(dirpath);
    // check permisions that we are alowed to be here
    if (!hasPermission(dirEntries[0], READ | EXECUTE)) {
        std::cerr << "Error: No read permission.\n";
        return -1;
    }

    // Update current directory block number
    if(path.size() == 0) {
        std::cerr << "Error: Invalid directory path.\n";
        return -1;
    }
    this->currentDir = dirEntries[0].first_blk;
    for (auto &&i : path)
    {
        if (dirpath == "..") {
            this->currentPath.pop_back();
        } else {
            this->currentPath.push_back(i);
        }
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
    // resolved filepath
    std::string fileName;
    FATEntry blk = this->currentDir;
    size_t pos = filepath.find_last_of("/");
    if(pos != std::string::npos) {
        std::string dirPath = filepath.substr(0, pos);  // Directory path
        fileName = filepath.substr(pos + 1);  // File name
        blk = resolvePath(dirPath);
    }
    else {
        fileName = filepath;
    }
    // Read the current directory block
    uint8_t block[BLOCK_SIZE] = { 0 };
    readBlock(blk, block);
    dir_entry* dirEntries = reinterpret_cast<dir_entry*>(block);

    // Find the source file
    dir_entry sourceEntry;
    uint16_t fileIndex = findDirEntry(dirEntries, sourceEntry, fileName);
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
