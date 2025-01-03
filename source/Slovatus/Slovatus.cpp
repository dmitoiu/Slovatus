/**
*
*  Copyright [2024] [Darie-Dragos Mitoiu]
*
* Licensed under the Slovatus License, Version 1.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.dmitoiu.ro/licenses/LICENSE-1.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
*/

#include <iostream>
#include <iomanip>
#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include <bitset>
#define CURL_STATICLIB
#include <curl/curl.h>
#include <json/json.h>
#include <mutex>

// Global constants
std::string GITHUB_API_URL;
int MIN_CONTRIBUTIONS;
int MAX_FOLLOWERS;
int SLEEP_TIME; // Sleep time in seconds
std::string ACCESS_TOKEN;

// Function to read environment variables from a .env file
std::map<std::string, std::string> load_env_file(const std::string& filename) {
    std::map<std::string, std::string> env_variables;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Error: Could not open .env file: " << filename << std::endl;
        return env_variables;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue; // Skip empty lines and comments
        size_t delimiter_pos = line.find('=');
        if (delimiter_pos != std::string::npos) {
            std::string key = line.substr(0, delimiter_pos);
            std::string value = line.substr(delimiter_pos + 1);
            env_variables[key] = value;
        }
    }

    file.close();
    return env_variables;
}

int loadConfig()
{
    const std::string env_file = ".env";
    try
    {
        auto env = load_env_file(env_file);

        if (env.empty()) {
            std::cerr << "Failed to load environment variables from " << env_file << std::endl;
            return 1;
        }
        GITHUB_API_URL = env["GITHUB_API_URL"];
        MIN_CONTRIBUTIONS = std::stoi(env["MIN_CONTRIBUTIONS"]);
        MAX_FOLLOWERS = std::stoi(env["MAX_FOLLOWERS"]);
        SLEEP_TIME = std::stoi(env["SLEEP_TIME"]); // Sleep time in seconds
        ACCESS_TOKEN = env["ACCESS_TOKEN"];
    } catch (std::exception &exception)
    {
        std::cout << exception.what() << std::endl;
    }
    return 0;
}

// Mutex for thread-safe operations
std::mutex api_mutex;

// Helper function for libcurl write callback
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    s->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Function to perform a GET request
bool perform_get_request(const std::string& url, std::string& response) {
    CURL* curl;
    CURLcode res;
    curl = curl_easy_init();

    if (!curl) {
        std::cerr << "Failed to initialize CURL." << std::endl;
        return false;
    }

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, ("Authorization: token " + ACCESS_TOKEN).c_str());
    headers = curl_slist_append(headers, "User-Agent: Slovatus/1.0");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);      // Treat HTTP errors as failures
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);       // Follow up to 5 redirects
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);        // Prevent signal handling issues in multithreaded apps
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L); // Add timeout for robustness
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        std::cerr << "CURL error: " << res << std::setw(2) << curl_easy_strerror(res) << std::endl;
        return false;
    }
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code >= 400) {
        std::cerr << "CURL HTTP error: " << http_code << std::endl;
        return false;
    }

    return true;
}

// Check if a user is already followed
bool is_user_followed(const std::string& username) {
    std::string url = GITHUB_API_URL + "/user/following/" + username;
    std::string response;
    long http_code = 0;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, ("Authorization: token " + ACCESS_TOKEN).c_str());
    headers = curl_slist_append(headers, "User-Agent: Slovatus/1.0");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request to check the resource existence
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);      // Treat HTTP errors as failures
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);       // Follow up to 5 redirects
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);        // Prevent signal handling issues in multithreaded apps
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L); // Add timeout for robustness
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Debug output for CURL

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    // HTTP 204 means "No Content," indicating the user is followed
    return (http_code == 204);
}


// Get repositories of a user
std::vector<Json::Value> get_user_repositories(const std::string& username) {
    std::vector<Json::Value> repositories;
    std::string url = GITHUB_API_URL + "/users/" + username + "/repos";
    std::string response;

    if (perform_get_request(url, response)) {
        Json::CharReaderBuilder reader;
        Json::Value jsonData;
        std::string errs;

        std::istringstream ss(response);
        if (Json::parseFromStream(reader, ss, &jsonData, &errs)) {
        	for (int i = 0; i < jsonData.size(); i++) {
                Json::Value repo = jsonData[i];
                repositories.push_back(repo);
            }
        }
        else {
            std::cerr << "JSON parse error: " << errs << std::endl;
        }
    }

    return repositories;
}

// Get total contributions of a user
int get_user_contributions(const std::string& username) {
    int total_contributions = 0;
    auto repositories = get_user_repositories(username);

    for (size_t i = 0; i < repositories.size(); ++i) {
        if (!repositories[i]["fork"].asBool()) {
            std::string repo_url = repositories[i]["url"].asString() + "/stats/contributors";
            std::string response;

            if (perform_get_request(repo_url, response)) {
                Json::CharReaderBuilder reader;
                Json::Value jsonData;
                std::string errs;

                std::istringstream ss(response);
                if (Json::parseFromStream(reader, ss, &jsonData, &errs)) {
                    for (int x = 0; x < jsonData.size(); x++) {
                        Json::Value contributor = jsonData[x];
                        if (contributor["author"]["login"].asString() == username) {
                            total_contributions += contributor["total"].asInt();
                        }
                    }
                }
            }
        }
    }

    return total_contributions;
}

// Follow a user
bool follow_user(const std::string& username) {
    CURL* curl;
    CURLcode res;
    curl = curl_easy_init();
    if (!curl) return false;

    std::string url = GITHUB_API_URL + "/user/following/" + username;
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, ("Authorization: token " + ACCESS_TOKEN).c_str());
    headers = curl_slist_append(headers, "User-Agent: Slovatus/1.0");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);      // Treat HTTP errors as failures
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);       // Follow up to 5 redirects
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);        // Prevent signal handling issues in multithreaded apps
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L); // Add timeout for robustness
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Debug output for CURL

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    return (res == CURLE_OK);
}

// Main function to manage following users
void manage_following() {
    int page = 1;
    int follow_count = 0;
    const short MAX_PAGE = 35;

    while (follow_count < MAX_FOLLOWERS) {
        std::string url = GITHUB_API_URL + "/search/users?q=type:user+repos:>0+followers:>100&page=" + std::to_string(page);
        std::string response;

        if(page == MAX_PAGE)
        {
	        page = 1;
        }

        if (!perform_get_request(url, response)) {
            std::cerr << "Failed to fetch URL: " << url << ". Retrying..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5)); // Retry delay
            continue;
        }

        Json::CharReaderBuilder reader;
        Json::Value jsonData;
        std::string errs;

        std::istringstream ss(response);
        if (Json::parseFromStream(reader, ss, &jsonData, &errs)) {
            if (jsonData["items"].empty()) {
                std::cout << "No more users to process." << std::endl;
                break;
            }
            std::lock_guard<std::mutex> guard(api_mutex);
            for (size_t i = 0; i < jsonData["items"].size(); ++i) {
                if (follow_count >= MAX_FOLLOWERS) {
                    std::cout << "Reached maximum follow limit of " << MAX_FOLLOWERS << "." << std::endl;
                    return;
                }

                std::string username = jsonData["items"][i]["login"].asString();

                if (is_user_followed(username)) {
                    std::cout << "Skipping already followed user: " << username << std::endl;
                    continue;
                }

                int contributions = get_user_contributions(username);
                if (contributions >= MIN_CONTRIBUTIONS) {
                    if (follow_user(username)) {
                        ++follow_count;
                        std::cout << "Followed " << username << " with " << contributions << " contributions. (Total follows: " << follow_count << ")" << std::endl;
                    }
                    else {
                        std::cerr << "Failed to follow " << username << std::endl;
                    }
                }
                else {
                    std::cout << username << " does not meet the contribution threshold." << std::endl;
                    std::cout << "Min Threshold: " << MIN_CONTRIBUTIONS << " User has: " << contributions << std::endl;
                }

                std::this_thread::sleep_for(std::chrono::seconds(SLEEP_TIME)); // Avoid rate limiting
            }
        }
        else {
            std::cerr << "JSON parse error: " << errs << std::endl;
            break;
        }

        ++page;
    }
}

std::string binary(std::string data)
{
    std::string result;
    for (char c : data)
        result += std::bitset<8>(c).to_string() + " ";
    return result;
}

void start()
{
    std::cout << "Press any key to start the bot...";
    std::cin.get();
}


int main()
{
    char separator = '-';
    std::cout << "Slovatus v1.0.0: GitHub Follow Bot" << std::endl;
    std::cout << std::string(71, separator) << std::endl;
    std::cout << binary("Slovatus") << std::endl;
    std::cout << std::string(71, separator) << std::endl;
    start();
    loadConfig();
    curl_global_init(CURL_GLOBAL_ALL); // Initialize CURL globally
    // Launch a thread to follow users
    std::thread follow_thread([] {
        manage_following();
    });
    follow_thread.join();
    curl_global_cleanup(); // Clean up CURL globally
	return 0;
}