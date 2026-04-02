/*
Usage: 
  char value[];
  String json = http.getString();
  const char* var = "find";
  getJsonValue(json.c_str(), var, value, 2);
  // 'value' contains returned value associated with variable

  s60sc 2026
*/

#include "appGlobals.h"

static std::string extractNestedValue(const std::string& jsonObject, const std::string& nestedKey);

static void skipWhitespace(const std::string& json, size_t& pos) {
  while (pos < json.length() && std::isspace(json[pos])) pos++;
}

static std::string parseString(const std::string& json, size_t& pos) {
  if (json[pos] != '"') return "";
  pos++; // Skip opening quote
  
  size_t start = pos;
  while (pos < json.length()) {
    if (json[pos] == '\\') pos += 2; // Skip escaped character
    else if (json[pos] == '"') {
      std::string result = json.substr(start, pos - start);
      pos++; // Skip closing quote
      return result;
    } else pos++;
  }
  return "";
}

static void skipValue(const std::string& json, size_t& pos) {
  if (json[pos] == '"') parseString(json, pos);
  else if (json[pos] == '{') {
    int depth = 1;
    pos++;
    while (pos < json.length() && depth > 0) {
      if (json[pos] == '"') parseString(json, pos);
      else if (json[pos] == '{') {
        depth++;
        pos++;
      } else if (json[pos] == '}') {
        depth--;
        pos++;
      } else pos++;
    }
  } else if (json[pos] == '[') {
    int depth = 1;
    pos++;
    while (pos < json.length() && depth > 0) {
      if (json[pos] == '"') parseString(json, pos);
      else if (json[pos] == '[') {
        depth++;
        pos++;
      } else if (json[pos] == ']') {
        depth--;
        pos++;
      } else pos++;
    }
  } else {
    // Number, boolean, null
    while (pos < json.length() && 
         json[pos] != ',' && 
         json[pos] != '}' && 
         json[pos] != ']') {
      pos++;
    }
  }
}

static std::string parseValue(const std::string& json, size_t& pos) {
  size_t start = pos;
  if (json[pos] == '"') return parseString(json, pos);
  else if (json[pos] == '{' || json[pos] == '[') {
    skipValue(json, pos);
    return json.substr(start, pos - start);
  } else {
    // Number, boolean, null - trim whitespace
    while (pos < json.length() && json[pos] != ',' 
      && json[pos] != '}' && json[pos] != ']' && !std::isspace(json[pos])) pos++;
  }
  std::string result = json.substr(start, pos - start);
  // Trim trailing whitespace
  size_t end = result.find_last_not_of(" \t\n\r");
  return (end != std::string::npos) ? result.substr(0, end + 1) : result;
}

// Recursively search through entire JSON structure for nth occurrence
static std::string findNthOccurrence(const std::string& json, size_t& pos, const std::string& key, int& occurrence, int targetOccurrence, bool extractNested = false, const std::string& nestedKey = "") {
  skipWhitespace(json, pos);
  if (pos >= json.length()) return "";
  if (json[pos] == '{') {
    pos++;
    
    while (pos < json.length()) {
      skipWhitespace(json, pos);
      
      if (json[pos] == '}') {
          pos++;
          break;
      }
      
      // Parse key
      std::string currentKey = parseString(json, pos);
      skipWhitespace(json, pos);
      
      if (pos >= json.length() || json[pos] != ':') return "";
      pos++;
      skipWhitespace(json, pos);
      
      // Check if this is our key
      if (currentKey == key) {
        occurrence++;
        if (occurrence == targetOccurrence) {
          std::string value = parseValue(json, pos);
          // If we need to extract a nested value from the object
          if (extractNested && !nestedKey.empty() && !value.empty() && value[0] == '{') {
            return extractNestedValue(value, nestedKey);
          }
          return value;
        } else skipValue(json, pos);
      } else {
        // Recursively search in nested structures
        if (json[pos] == '{' || json[pos] == '[') {
          std::string result = findNthOccurrence(json, pos, key, occurrence, targetOccurrence, extractNested, nestedKey);
          if (!result.empty()) return result;
        } else skipValue(json, pos);
      }
      
      skipWhitespace(json, pos);
      if (pos < json.length() && json[pos] == ',') pos++;
    }
  } else if (json[pos] == '[') {
    pos++;
    
    while (pos < json.length()) {
      skipWhitespace(json, pos);
      
      if (json[pos] == ']') {
        pos++;
        break;
      }
      
      // Recursively search array elements
      if (json[pos] == '{' || json[pos] == '[') {
        std::string result = findNthOccurrence(json, pos, key, occurrence, targetOccurrence, extractNested, nestedKey);
        if (!result.empty()) return result;
      } else skipValue(json, pos);

      skipWhitespace(json, pos);
      if (pos < json.length() && json[pos] == ',') pos++;
    }
  }
  return "";
}

// New helper function to extract a nested value
static std::string extractNestedValue(const std::string& jsonObject, const std::string& nestedKey) {
  size_t pos = 0;
  int count = 0;
  return findNthOccurrence(jsonObject, pos, nestedKey, count, 1);
}

// Updated function signatures
bool getJsonValue(const char* json, const char* key, char* value, const char* nestedKey, int occurrence) {
  // Returns nth occurrence (1-indexed, default is 1)
  // If nestedKey is provided, extracts that field from the object value
  std::string jsonStr = json;
  std::string keyStr = key;
  if (occurrence < 1) occurrence = 1;
  size_t pos = 0;
  int count = 0;
  
  bool extractNested = (nestedKey != nullptr && strlen(nestedKey) > 0);
  std::string nestedKeyStr = extractNested ? nestedKey : "";
  
  std::string retvalue = findNthOccurrence(jsonStr, pos, keyStr, count, occurrence, extractNested, nestedKeyStr);
  strncpy(value, retvalue.c_str(), FILE_NAME_LEN - 1);
  value[FILE_NAME_LEN - 1] = '\0';
  return retvalue.length() ? true : false;
}