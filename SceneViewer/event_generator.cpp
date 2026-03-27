#include <iostream>
#include <fstream>
#include <string>

/**
 * Writes a specific file a set number of times.
 * If the file does not exist, it will be created.
 */
void write_to_file(const std::string &file_name, const std::string &text, uint32_t count)
{
    std::ofstream outFile(file_name, std::ios::out | std::ios::app);

    if (!outFile)
    {
        std::cerr << "[event_generator.cpp]: Could not open or create " << file_name << std::endl;\
        return;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        outFile << text << "\n";
    }

    outFile.close();

    std::cout << "[event_generator.cpp]: Successfully wrote to " << file_name << " " << count << " times." << std::endl;
}

int main()
{
    std::string myFile = "event.txt";
    std::string myLine = "AVAILABLE 0.016667";
    uint32_t repeat = 1000;
    
    write_to_file(myFile, myLine, repeat);

    return 0;
}