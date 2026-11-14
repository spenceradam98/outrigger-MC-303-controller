// Fixed-capacity MIDI event list: no allocation on the audio thread.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace outrigger
{
struct Event
{
    int offset = 0;          // sample offset inside the block
    uint8_t size = 0;
    uint8_t bytes[16] {};
    uint8_t status() const { return bytes[0]; }
};

class EventList
{
public:
    static constexpr int kCapacity = 2048;

    void clear() { count = 0; }
    int size() const { return count; }
    const Event& operator[] (int i) const { return events[(size_t) i]; }
    Event* begin() { return events.data(); }
    Event* end() { return events.data() + count; }
    const Event* begin() const { return events.data(); }
    const Event* end() const { return events.data() + count; }

    bool add (int offset, const uint8_t* data, int n)
    {
        if (count >= kCapacity || n <= 0 || n > 16) return false;
        auto& e = events[(size_t) count++];
        e.offset = offset;
        e.size = (uint8_t) n;
        std::memcpy (e.bytes, data, (size_t) n);
        return true;
    }
    bool add3 (int offset, uint8_t a, uint8_t b, uint8_t c) { const uint8_t d[3] { a, b, c }; return add (offset, d, 3); }
    bool add2 (int offset, uint8_t a, uint8_t b)            { const uint8_t d[2] { a, b }; return add (offset, d, 2); }
    bool add1 (int offset, uint8_t a)                       { return add (offset, &a, 1); }

    // Stable sort by offset (insertion sort; lists are short and mostly ordered).
    void sortByOffset()
    {
        for (int i = 1; i < count; ++i)
        {
            Event key = events[(size_t) i];
            int j = i - 1;
            while (j >= 0 && events[(size_t) j].offset > key.offset) { events[(size_t) j + 1] = events[(size_t) j]; --j; }
            events[(size_t) j + 1] = key;
        }
    }

private:
    std::array<Event, kCapacity> events {};
    int count = 0;
};

// Bytes a message costs on a 31.25 kbaud DIN link (no running status assumed).
inline int wireBytes (const Event& e) { return e.size; }
} // namespace outrigger
