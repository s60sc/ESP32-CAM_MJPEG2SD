/********************** Manage SD card memory***********************/

/*  
    Call 
    checkSDcard(SD_MMC, "/",1); 
    function anywhere to check and clean the SD card, this process does not require user intervention.
*/

#define manageSDcardMem 1 // Uncomment to enable the memory cleaning functionality

#ifdef manageSDcardMem

#include <iostream>

//for the SD card management part, it will delete older files if SD card memory is almost full
#define startCleaningAt 200 // value in MB, if the memory available is less than this value the older files from the SD card will be deleted
#define cleanMB 1024 // size in MB of how much memory we want to free up

#define SHOW_LOG 1  //uncomment to view logs/debug using serial communication

bool cardAlmostFull(bool type)
{
  uint64_t totBytes, useBytes = 0;
  totBytes = SD_MMC.totalBytes() / (1024 * 1024);  // All the usable space
  useBytes = SD_MMC.usedBytes() / (1024 * 1024); // Used space

  if (type) {
    if ( (totBytes - useBytes) <= startCleaningAt) {  // To check if the space available is under a certain treshold
      return 1; // yup card is almost full
    }
    return 0; //card still has space available
  } else { //if type == 0
    if ( (totBytes - useBytes) >= (startCleaningAt + cleanMB)) {  // To check if enough space was cleaned
      return 0; // return 0 if we have enough space available
    }
    return 1; //return 1 if there ins't enough space available
  }
}

// function from https://stackoverflow.com/questions/8888748/how-to-check-if-given-c-string-or-char-contains-only-digits
bool is_digits(const std::string &str)
{
  return str.find_first_not_of("0123456789/") == std::string::npos;
}

// Next three functions are from (and based on) the SDMMC_Test example from the ESP32 SD_MMC library
void removeDir(fs::FS &fs, const char * path) {
#ifdef SHOW_LOG
  Serial.printf("Removing Dir: %s\n", path);
#endif
  if (fs.rmdir(path)) {
#ifdef SHOW_LOG
    Serial.println("Dir removed");
#endif
  } else {
#ifdef SHOW_LOG
    Serial.println("rmdir failed");
#endif
  }
}

void deleteFile(fs::FS &fs, const char * path) {
#ifdef SHOW_LOG
  Serial.printf("Deleting file: %s\n", path);
#endif
  if (fs.remove(path)) {
#ifdef SHOW_LOG
    Serial.println("File deleted");
#endif
  } else {
#ifdef SHOW_LOG
    Serial.println("Delete failed");
#endif
  }
}


#define MSG_ENOUGH_MEMORY 0
#define MSG_NO_ENOUGH_MEMORY 1
#define MSG_DELETE_FOLDER 2
#define MSG_CHECK_MEMORY 3

byte cleanFile(fs::FS &fs, const char * dirname) {
  // VERIFY what dirname contains and compare to what it should contain
#ifdef SHOW_LOG
  Serial.print("Cleaning directory named: ");
  Serial.println(dirname);
#endif

  File root = fs.open(dirname);
  if (!root) {
#ifdef SHOW_LOG
    Serial.println("Failed to open directory during cleaning");
#endif
    return 5;
  }
  if (!root.isDirectory()) {
#ifdef SHOW_LOG
    Serial.println("Not a directory during cleaning");
#endif
    return 5;
  }

  File file = root.openNextFile();

  bool emptyFolderCheck = 0;

  while (file) {
    emptyFolderCheck = 1;
#ifdef SHOW_LOG
    Serial.println();
    Serial.print("  Now checking FILE: ");
    Serial.print(file.name());
    Serial.print("  SIZE: ");
    Serial.println(file.size());
#endif

    if (cardAlmostFull(0) == 1) {
      deleteFile(SD_MMC, file.name());
#ifdef SHOW_LOG
      Serial.println("-> File deleted");
#endif
    } else {
#ifdef SHOW_LOG
      Serial.println("-> No files deleted");
#endif
      break;
    }
    file = root.openNextFile();
  }

  if (emptyFolderCheck == 0) {
    // Must delete empty folder here
#ifdef SHOW_LOG
    Serial.println("Empty folder found");
#endif
    removeDir(SD_MMC, dirname);
    return MSG_CHECK_MEMORY;
  }

  if (cardAlmostFull(0) == 1) {
    return MSG_NO_ENOUGH_MEMORY;
  } else {
    return MSG_ENOUGH_MEMORY;
  }
  return MSG_NO_ENOUGH_MEMORY;
}

// --------

uint16_t howManyFolders(fs::FS &fs, const char * dirname) {
  uint16_t folders = 0;

  File root = fs.open(dirname);
  if (!root) {

#ifdef SHOW_LOG
    Serial.println("Failed to open directory [uint16_t howManyFolders]");
#endif

    return 0;
  }
  if (!root.isDirectory()) {

#ifdef SHOW_LOG
    Serial.println("Not a directory [uint16_t howManyFolders]");
#endif

    return 0;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      //Serial.print("  DIR : ");
      //Serial.println(file.name());
      folders++;
    }
    file = root.openNextFile();
  }
  return folders;
}

void checkSDcard(fs::FS &fs, const char * dirname, uint8_t levels) {
  if (cardAlmostFull(1) == 0) {
#ifdef SHOW_LOG
    Serial.printf("SD card has enough memory available.");
#endif
    return;
  }
#ifdef SHOW_LOG
  Serial.printf("Listing recordings directories: %s\n", dirname);
#endif

  static bool cleaning = 0;

  File root = fs.open(dirname);
  if (!root) {

#ifdef SHOW_LOG
    Serial.println("Failed to open directories [void checkSDcard]");
#endif

    return;
  }
  if (!root.isDirectory()) {

#ifdef SHOW_LOG
    Serial.println("Not a directory [void checkSDcard]");
#endif

    return;
  }

  // The maximun number of folders for a FAT32 filesystem is 65536 if only 11 characters are used (at maximun) in folder names
  uint16_t foldersNumber = howManyFolders(SD_MMC, "/");
  uint16_t recFolders = 0;
  // Folder names in FAT32 filesystem will have a 11 character name at maximun
  // The folder names this sketch creates have 9 characters
  //char folderNames[foldersNumber][10]; // delete##

  char folderNames[foldersNumber + 1][11];

  File file = root.openNextFile();

  uint32_t i = 0;
  // Copy the name, of the video holding directories, to a 2D char array
  while (1) {
    if (file.isDirectory()) {
      //Serial.print("  DIR : ");
      //Serial.println(file.name());
      if (is_digits(file.name()) == 1) {
        strcpy(folderNames[i], file.name());
        recFolders++;

#ifdef SHOW_LOG
        Serial.println(folderNames[i]);
#endif

        i++;
      }
    } else {
      break;
    }
    file = root.openNextFile();
  }

  // Sort out the names inside the 2D char array
  char buff[11];

  uint16_t e = 0, o = 0;
  for (i = 0; i < recFolders ; i++) {

    for (e = 0; e < recFolders ; e++) {

      for (o = 0; o < 9 ; o++) {
        byte first = int(folderNames[e][o]), secon = int(folderNames[e + 1][o]);
        if ((e < (recFolders - 1)) && (first > secon)) {
          //          Serial.println();
          //          Serial.print(folderNames[e]);
          //          Serial.print(" is higher than ");
          //          Serial.print(folderNames[e+1]);

          strcpy(buff, folderNames[e]);

          //        Serial.println();
          //        Serial.print("buff: ");
          //        Serial.print(buff);
          //        Serial.print(" <- ");
          //        Serial.print(" folderNames[");
          //        Serial.print(e);
          //        Serial.print("]: ");
          //        Serial.print(folderNames[e]);

          strcpy(folderNames[e], folderNames[e + 1]);

          //        Serial.println();
          //        Serial.print("folderNames[e]: ");
          //        Serial.print(folderNames[e]);
          //        Serial.print(" <- ");
          //        Serial.print(" folderNames[");
          //        Serial.print(e);
          //        Serial.print("+1]: ");
          //        Serial.print(folderNames[e+1]);

          strcpy(folderNames[e + 1], buff);

          //        Serial.println();
          //        Serial.print("buff: ");
          //        Serial.print(buff);
          //        Serial.print(" -> ");
          //        Serial.print(" folderNames[");
          //        Serial.print(e+1);
          //        Serial.print("]: ");
          //        Serial.print(folderNames[e+1]);

          break;
        } else if ((e < (recFolders - 1)) && (first < secon)) {
          break;
        }
      }
    }
  }

#ifdef SHOW_LOG
  Serial.println();
  Serial.println("Sorted list:");

  for (i = 0; i < recFolders; i++) {
    Serial.println(folderNames[i]);
  }

  Serial.println();
  Serial.println("Cleaning started... ");
#endif

  for (i = 0; i < recFolders; i++) {

    byte msg = 0;
    msg = cleanFile(SD_MMC, folderNames[i]);

#ifdef SHOW_LOG
    switch (msg) {
      case MSG_ENOUGH_MEMORY:
        Serial.println("Enough memory");
        break; // End cleaning

      case MSG_NO_ENOUGH_MEMORY:
        Serial.println("No enough memory");
        break;

      case MSG_CHECK_MEMORY:
        Serial.println("Check memory");
        break;
    }
#endif

    if (msg == MSG_ENOUGH_MEMORY) {
#ifdef SHOW_LOG
      Serial.println("Enough memory available.");
#endif
      return;
    } else if (msg == MSG_CHECK_MEMORY) {
      if (cardAlmostFull(1) == 1) {
#ifdef SHOW_LOG
        Serial.println("Enough memory available.");
#endif
        return; // End cleaning
      }
    }
  }
#ifdef SHOW_LOG
  Serial.println("Cleaning process may have failed..");
#endif
}


#endif
