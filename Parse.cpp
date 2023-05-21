#include "VertexData.h"

#include <string>
#include <iomanip>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <sstream>

struct Entry {
    uint64_t proc = 0;
    uint64_t thread = 0;
    uint64_t start = 0;
    uint64_t length = 0;
    uint32_t name_index = 0;
    uint32_t vert_index = 0;
};

std::vector<Entry> g_alltasks;
std::vector< std::vector< std::vector< Entry * > > > g_tasksperproc;
std::vector<std::string> g_names;
std::vector< float > rowpos;
std::vector< std::pair< uint64_t, uint64_t > > rowdata;

static bool starts_with(const std::string & value1, const std::string & value2) {
    return value1.find(value2) == 0;
}

void split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
}

static void get_color(Entry &e, color_t &c) {
    color_t cols[] = { 
        color_t{.5f,.0f,.0f}, color_t{.0f,.5f,.0f}, color_t{.0f,.0f,.5f},
        color_t{.5f,.5f,.0f}, color_t{.5f,.0f,.5f}, color_t{.0f,.5f,.5f},
        color_t{.5f,.5f,.5f}, color_t{.0f,.0f,.0f},
        color_t{.5f,.3f,.0f}, color_t{.3f,.5f,.0f}, color_t{.0f,.3f,.5f},
        color_t{.5f,.0f,.3f}, color_t{.0f,.5f,.3f}, color_t{.3f,.0f,.5f},
        color_t{.5f,.5f,.3f}, color_t{.5f,.3f,.5f}, color_t{.3f,.5f,.5f}
    };

    const size_t idx = e.name_index % (sizeof(cols)/sizeof(cols[0]));
    c = cols[idx];
}

bool generate_triangles(std::vector<vertex_t> & vertices, std::vector<uint32_t> & indices_line, std::vector<uint32_t> & indices_tri) {
    if (false) {
        vertices.clear();
        vertices.push_back({{ 0.0f, 0.5f }, { 1.0f, 0.0f, 0.0f }});
        vertices.push_back({{ 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f }});
        vertices.push_back({{ 0.0f,-0.5f }, { 0.0f, 0.0f, 1.0f }});

        indices_line.push_back(0);
        indices_line.push_back(1);
        indices_line.push_back(1);
        indices_line.push_back(2);
        indices_line.push_back(2);
        indices_line.push_back(0);

        indices_tri.push_back(0);
        indices_tri.push_back(1);
        indices_tri.push_back(2);
        return true;
    }

    size_t row = 0;
    const float rowheight = 1.0f;
    const float barheight = 0.8f;
    const float proc_distance = 2.5f;
    float extra_height = 0.0f;

    for (size_t proc = 0; proc < g_tasksperproc.size(); ++proc) {
        for (size_t thread = 0; thread < g_tasksperproc[proc].size(); ++thread, ++row) {

            const float y = extra_height + row * rowheight + 0.5f;
            const float y0 = y - barheight / 2.0f;
            const float y1 = y + barheight / 2.0f;
            rowpos.push_back(y);
            rowdata.push_back(std::make_pair(proc, thread));

            for (size_t i = 0; i < g_tasksperproc[proc][thread].size(); ++i) {
                const float start = 1e-3f * (float) (g_tasksperproc[proc][thread][i]->start);
                const float end = 1e-3f * (float) (g_tasksperproc[proc][thread][i]->start + g_tasksperproc[proc][thread][i]->length);

                color_t col;
                get_color(*g_tasksperproc[proc][thread][i], col);

                const uint32_t idx = (uint32_t) vertices.size();
                vertices.push_back({ {start, y0}, col });
                vertices.push_back({ {end, (y0 + y1) / 2.0f}, col });
                vertices.push_back({ {start, y1}, col });

                g_tasksperproc[proc][thread][i]->vert_index = idx;

                indices_line.push_back(idx);
                indices_line.push_back(idx+1);
                indices_line.push_back(idx+1);
                indices_line.push_back(idx+2);
                indices_line.push_back(idx+2);
                indices_line.push_back(idx);

                indices_tri.push_back(idx);
                indices_tri.push_back(idx+1);
                indices_tri.push_back(idx+2);
            }
        }
        extra_height += proc_distance;
    }
    return true;
}

static void ReadUntilNewline(const char *& ptr) {
    while (*ptr != '\n' && *ptr != '\r') {
        ++ptr;
    }
};

static void ReadNewline(const char *& ptr) {
    while (*ptr == '\n' || *ptr == '\r') {
        ++ptr;
    }
};

static void SkipLine(const char *& ptr) {
    ReadUntilNewline(ptr);
    ReadNewline(ptr);
};

static const char * Find(const char * ptr, const char * end, char c) {
    while (*ptr != c && ptr < end) {
        ++ptr;
    }
    return ptr;
}

std::string format(uint64_t a) {
    std::stringstream ss;
    ss << a;
    std::string r = ss.str();
    if (r.size() > 6)
        r = r.substr(0, r.size()-6) + "." + r.substr(r.size()-6);
    else {
        while (r.size() < 6)
            r = "0" + r;
        r = "0." + r;
    }
    return r;
}

bool parse(const char * filename, std::vector<vertex_t> & vertices, std::vector<uint32_t> & indices_line, std::vector<uint32_t> & indices_tri) {
    std::ifstream infile(filename, std::ios::binary | std::ios::ate);
    std::cout << filename << std::endl;
    if (infile.fail()) {
        std::cerr << "Read failed" << std::endl;
        return false;
    }

    std::streamsize size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!infile.read(buffer.data(), size)) {
        std::cerr << "Read failed" << std::endl;
        return false;
    }


    const char * ptr = buffer.data();
    const char * end = buffer.data() + size;

    int numLines = 0;
    for (const char * i = ptr; i < end; ++i) {
        if (*i == '\n') {
            ++numLines;
        }
    }

    std::cout << "numLines=" << numLines << std::endl;

    g_alltasks.reserve(numLines);
    std::unordered_map<uint64_t, int> name_index;

    while (ptr < end) {
        char * next;
        if (*ptr == '.') { // name
            ++ptr;
            uint64_t val = strtoull(ptr, &next, 16);
            if (*next != ' ') {
                std::cerr << "Parse error!" << std::endl;
                return false;
            }
            ptr = next + 1;
            ReadUntilNewline(ptr);
            if (name_index.find(val) == name_index.end()) {
                g_names.push_back(std::string((const char *) next + 1, ptr));
                name_index[val] = (int) g_names.size() - 1;
            }
            ReadNewline(ptr);
            continue;
        }
        if (*ptr == '#') {
            SkipLine(ptr);
            continue;
        }

        Entry e;
        e.thread = strtoull(ptr, &next, 10);
        e.start = strtoull(next + 1, &next, 10);
        e.length = strtoull(next + 1, &next, 10);
        const uint64_t name = strtoull(next + 1, &next, 16);
        e.name_index = name_index[name];
        g_alltasks.push_back(e);

        ptr = next;
        ReadNewline(ptr);
    }

    infile.close();

    if (g_alltasks.empty()) {
        std::cerr << "Empty log" << std::endl;
        return false;
    }

    std::cout << "parsed." << std::endl;

    // fix process ids
    std::sort(g_alltasks.begin(), g_alltasks.end(), [](const Entry & a, const Entry & b) -> bool {
        if (a.proc != b.proc) {
            return a.proc < b.proc;
        }
        if (a.thread != b.thread) {
            return a.thread < b.thread;
        }
        return a.start < b.start;
    });

    std::cout << "sorted." << std::endl;

    uint64_t currentProc = g_alltasks[0].proc;
    uint64_t currentThread = g_alltasks[0].thread;
    uint64_t procCounter = 0;
    uint64_t threadCounter = 0;
    g_tasksperproc.resize(1);
    g_tasksperproc[0].resize(1);
    g_tasksperproc[procCounter][threadCounter].push_back(&g_alltasks[0]);
    g_alltasks[0].proc = 0;
    g_alltasks[0].thread = 0;
    for (size_t i = 1; i < g_alltasks.size(); ++i) {
        if (g_alltasks[i].proc == currentProc) {
            g_alltasks[i].proc = procCounter;
            if (g_alltasks[i].thread == currentThread) {
                g_alltasks[i].thread = threadCounter;
            }
            else {
                currentThread = g_alltasks[i].thread;
                g_alltasks[i].thread = ++threadCounter;
                g_tasksperproc[procCounter].push_back(std::vector< Entry * >());
            }
        }
        else {
            currentProc = g_alltasks[i].proc;
            g_alltasks[i].proc = ++procCounter;
            currentThread = g_alltasks[i].thread;
            threadCounter = 0;
            g_alltasks[i].thread = threadCounter;
            g_tasksperproc.push_back(std::vector< std::vector< Entry * > >());
            g_tasksperproc[procCounter].push_back(std::vector< Entry * >());
        }
        g_tasksperproc[procCounter][threadCounter].push_back(&g_alltasks[i]);
    }

    std::vector<int> numtasks;
    numtasks.resize(g_names.size(), 0);
    std::vector<uint64_t > totaltimes;
    totaltimes.resize(g_names.size(), 0);

    for (size_t i = 0; i < g_alltasks.size(); ++i) {
        const int index = g_alltasks[i].name_index;
        ++numtasks[index];
        totaltimes[index] += g_alltasks[i].length;
    }

    std::cout << "binned." << std::endl;

    // normalize start time
    size_t num_procs = procCounter + 1;

    std::vector<uint64_t> starttimes(num_procs);
    for (int i = 0; i < g_tasksperproc.size(); ++i) {
        uint64_t start = 0;
        bool first = true;
        for (int j = 0; j < g_tasksperproc[i].size(); ++j) {
            if (g_tasksperproc[i][j].empty()) {
                continue;
            }
            if (first || g_tasksperproc[i][j][0]->start < start) {
                first = false;
                start = g_tasksperproc[i][j][0]->start;
            }
        }
        starttimes[i] = start;
    }

    std::cout << "normalized." << std::endl;

    uint64_t endtime = 0;
    uint64_t totaltime = 0;
    for (size_t i = 0; i < g_alltasks.size(); ++i) {
        g_alltasks[i].start -= starttimes[g_alltasks[i].proc];
        if (g_alltasks[i].start + g_alltasks[i].length > endtime)
            endtime = g_alltasks[i].start + g_alltasks[i].length;
        totaltime += g_alltasks[i].length;
    }

    std::cout << "#tasks=" << g_alltasks.size()
              << " endtime=" << format(endtime)
              << " time=" << format(totaltime)
              << " parallelism=" << std::setprecision(3) << std::fixed
              << totaltime / (float)endtime
              << std::endl;

    std::cout << std::endl;

    std::vector<size_t> index(g_names.size());
    for (size_t i = 0; i < index.size(); ++i) {
        index[i] = i;
    }

    std::sort(index.begin(), index.end(), [&totaltimes](size_t a, size_t b) -> bool {
        return totaltimes[a] < totaltimes[b];
    });

    for (size_t i = 0; i < index.size(); ++i) {
        size_t idx = index[i];
        std::cout << std::left << std::setw(40) << g_names[idx]
                  << std::right << std::setw(10) << format(totaltimes[idx])
                  << std::setw(10) << numtasks[idx] << std::endl;
    }

    std::cout << "generating." << std::endl;
    return generate_triangles(vertices, indices_line, indices_tri);
}
