#include <gc.h>

inline void * safe_realloc(void * ptr, size_t len)
{
    if (!ptr) return GC_malloc(len);
    if (len == 0) { GC_free(ptr); return 0; }
    return GC_realloc(ptr, len);
}

struct ShortString {
    static const size_t mcap = 256-sizeof(size_t);
    size_t mlength = 0;
    char bytes[mcap] = {0};
    
    char & operator[](size_t pos) noexcept { return bytes[pos]; }
    const char & operator[](size_t pos) const noexcept { return bytes[pos]; }
    char * data() noexcept { return bytes; }
    const char * data() const noexcept { return bytes; }
    
    size_t size() const noexcept { return mlength; }
    size_t length() const noexcept { return size(); }
    size_t capacity() const noexcept { return mcap; }
    char * begin() noexcept { return bytes; }
    char * end() noexcept { return bytes + mlength; }
    const char * begin() const noexcept { return bytes; }
    const char * end() const noexcept { return bytes + mlength; }
    
    bool empty() const noexcept { return mlength == 0; }
    
    char & front() { return bytes[0]; }
    char & back() { return bytes[mlength - 1]; }
    const char & front() const { return bytes[0]; }
    const char & back() const { return bytes[mlength - 1]; }
    
    ShortString & operator=(const ShortString & other)
    {
        memcpy(this, &other, sizeof(ShortString));
        return *this;
    }
    
    bool starts_with(const ShortString & b)
    {
        if (size() < b.size())
            return false;
        return strncmp(data(), b.data(), b.size()) == 0;
    }
    bool starts_with(const char * b)
    {
        size_t b_size = strlen(b);
        if (size() < b_size)
            return false;
        return strncmp(data(), b, b_size) == 0;
    }
    
    bool operator==(const ShortString & other) const
    {
        if (other.size() != size())
            return false;
        for (size_t i = 0; i < size(); i++)
        {
            if ((*this)[i] != other[i])
                return false;
        }
        return true;
    }
    
    bool operator==(const char * other) const
    {
        size_t i = 0;
        for (i = 0; i < size() && other[i] != 0; i++)
        {
            if ((*this)[i] != other[i])
                return false;
        }
        return i == size() && other[i] == 0;
    }
    bool operator!=(const ShortString & other) const { return !(*this == other); }
    bool operator!=(const char * other) const { return !(*this == other); }
    
    bool operator<(const ShortString & other) const
    {
        for (size_t i = 0; i < size() && i < other.size(); i++)
        {
            if ((*this)[i] != other[i])
                return (*this)[i] < other[i];
        }
        return size() < other.size();
    }
    
    ShortString & operator+=(const ShortString & other) &
    {
        for (size_t i = 0; i < other.size(); i++)
        {
            bytes[mlength++] = other[i];
            assert(mlength < mcap);
        }
        return *this;
    }
    ShortString & operator+=(char c) &
    {
        char chars[2];
        chars[0] = c;
        chars[1] = 0;
        return *this += ShortString(chars);
    }
    
    ShortString operator+(const ShortString & other) const&
    {
        ShortString l = *this;
        l += other;
        return l;
    }
    ShortString operator+(const char * other) const&
    {
        const auto d = ShortString(other);
        return *this + d;
    }
    
    ShortString substr(size_t pos, size_t len = (size_t)-1) const&
    {
        if (len == (size_t)-1)
            len = size() - pos;
        if (ptrdiff_t(pos) < 0)
            pos += size();
        if (ptrdiff_t(pos) < 0 || pos >= size())
            return ShortString();
        len = len < size() - pos ? len : size() - pos;
        ShortString ret;
        for (size_t i = 0; i < len; i++)
            ret += (*this)[pos + i];
        return ret;
    }
    
    constexpr ShortString() noexcept { }
    ShortString(const ShortString & other)
    {
        memcpy(this, &other, sizeof(ShortString));
    }
    ShortString(const char * data)
    {
        size_t len = 0;
        while (data[len++] != 0);
        mlength = len - 1;
        assert(mlength < 256-sizeof(size_t)-1);
        memmove(bytes, data, mlength);
    }
    ~ShortString() noexcept { }
};

ShortString operator+(const char * other, const ShortString & right)
{
    const auto d = ShortString(other);
    return d + right;
}
 
// vector where destructors, copies, moves, and constructors are not necessarily run
// T must be malloc-aligned and POD
// all-zeroes must be a valid default state of T
template<typename T, int growth_factor = 20> // growth factor is out of 10. a value of 20 means 2x. must be at least 11.
struct PODVec
{
    T * mbuffer = 0;
    size_t mlength = 0;
    size_t mcapacity = 0;
    
    PODVec() { }
    PODVec(size_t count, const T & value = T())
    {
        mlength = count;
        mbuffer = (T *)GC_malloc(sizeof(T) * mlength);
        for (size_t i = 0; i < count; i++)
            mbuffer[i] = value;
    }
    PODVec(const PODVec & other)
    {
        mlength = other.mlength;
        mcapacity = other.mcapacity;
        if (mlength)
            mbuffer = (T *)GC_malloc(sizeof(T) * mcapacity);
        if (mbuffer)
            memcpy(mbuffer, other.mbuffer, sizeof(T) * mlength);
    }
    PODVec(PODVec && other)
    {
        mbuffer = other.mbuffer; other.mbuffer = 0;
        mlength = other.mlength; other.mlength = 0;
        mcapacity = other.mcapacity; other.mcapacity = 0;
    }
    
    ~PODVec() { if(mbuffer) GC_free(mbuffer); mbuffer = 0; }
    
    NOINLINE PODVec & operator=(const PODVec & other)
    {
        if (mbuffer) GC_free(mbuffer); mbuffer = 0;
        mlength = other.mlength;
        mcapacity = other.mcapacity;
        if (mlength)
            mbuffer = (T *)GC_malloc(sizeof(T) * mlength);
        if (mbuffer)
            memcpy(mbuffer, other.mbuffer, sizeof(T) * mlength);
        return *this;
    }
    PODVec & operator=(PODVec && other)
    {
        if (mbuffer) GC_free(mbuffer); mbuffer = 0;
        mbuffer = other.mbuffer; other.mbuffer = 0;
        mlength = other.mlength; other.mlength = 0;
        mcapacity = other.mcapacity; other.mcapacity = 0;
        return *this;
    }
    
    size_t size() const noexcept { return mlength; }
    size_t capacity() const noexcept { return mlength; }
    T * data() noexcept { return (T*)mbuffer; }
    const T * data() const noexcept { return (T*)mbuffer; }
    T * begin() noexcept { return (T*)mbuffer; }
    T * end() noexcept { return ((T*)mbuffer) + mlength; }
    const T * begin() const noexcept { return (T*)mbuffer; }
    const T * end() const noexcept { return ((T*)mbuffer) + mlength; }
    T & front() noexcept { return mbuffer[0]; }
    T & back() noexcept { return mbuffer[mlength-1]; }
    const T & front() const noexcept { return mbuffer[0]; }
    const T & back() const noexcept { return mbuffer[mlength-1]; }
    
    const T & operator[](size_t pos) const noexcept { return ((T*)mbuffer)[pos]; }
    T & operator[](size_t pos) noexcept { return ((T*)mbuffer)[pos]; }
    constexpr bool operator==(const PODVec<T> & other) const
    {
        if (other.mlength != mlength)
            return false;
        for (size_t i = 0; i < mlength; i++)
        {
            if (!((*this)[i] == other[i]))
                return false;
        }
        return true;
    }
    const T & at(size_t pos) const noexcept { assert(pos < mlength); return ((T*)mbuffer)[pos]; }
    T & at(size_t pos) noexcept { assert(pos < mlength); return ((T*)mbuffer)[pos]; }
    
    void push_back(const T & item)
    {
        size_t oldcap = mcapacity;
        mlength += 1;
        if (mlength > mcapacity)
            mcapacity = (mcapacity * growth_factor + 9) / 10;
        if (mlength > mcapacity) mcapacity = mlength;
    
        if (mcapacity != oldcap)
            mbuffer = (T *)safe_realloc(mbuffer, sizeof(T) * mcapacity);
        
        memset(mbuffer + (mlength-1), 0, sizeof(T));
        mbuffer[mlength-1] = item;
    }
    void push_back(T && item)
    {
        size_t oldcap = mcapacity;
        mlength += 1;
        if (mlength > mcapacity)
            mcapacity = (mcapacity * growth_factor + 9) / 10;
        if (mlength > mcapacity) mcapacity = mlength;
    
        if (mcapacity != oldcap)
            mbuffer = (T *)safe_realloc(mbuffer, sizeof(T) * mcapacity);
        
        //memcpy(mbuffer + (mlength-1), &item, sizeof(T));
        memset(mbuffer + (mlength-1), 0, sizeof(T));
        mbuffer[mlength-1] = std::move(item);
        //memset(&item, 0, sizeof(T));
    }
    T pop_back()
    {
        mlength -= 1;
        T ret = std::move(mbuffer[mlength]);
        memset(mbuffer + mlength, 0, sizeof(T));
        return ret;
    }
    
    NOINLINE void insert_at(size_t i, T && item)
    {
        size_t oldcap = mcapacity;
        mlength += 1;
        if (mlength > mcapacity)
            mcapacity = (mcapacity * growth_factor + 9) / 10;
        if (mlength > mcapacity) mcapacity = mlength;
        
        if (mcapacity != oldcap)
            mbuffer = (T *)safe_realloc(mbuffer, sizeof(T) * mcapacity);
        
        memset(mbuffer + (mlength-1), 0, sizeof(T));
        memmove(mbuffer + i + 1, mbuffer + i, (mlength - i - 1) * sizeof(T));
        
        //memcpy(mbuffer + i, &item, sizeof(T));
        memset(mbuffer + i, 0, sizeof(T));
        mbuffer[i] = std::move(item);
        //memset(&item, 0, sizeof(T));
    }
    NOINLINE void erase_at(size_t i)
    {
        memmove(mbuffer + i, mbuffer + i + 1, (mlength - i - 1) * sizeof(T));
        mlength -= 1;
        memset(mbuffer + mlength, 0, sizeof(T));
    }
    
    void clear() { GC_free(mbuffer); mbuffer = 0; mlength = 0; mcapacity = 0; }
};

template<typename T0, typename T1>
struct Pair {
    T0 _0;
    T1 _1;
    Pair(const T0 & a, const T1 & b) : _0(a), _1(b) { }
    Pair(T0 && a, T1 && b) : _0(std::move(a)), _1(std::move(b)) { }
    Pair(const T0 & a, T1 && b) : _0(a), _1(std::move(b)) { }
    Pair(T0 && a, const T1 & b) : _0(std::move(a)), _1(b) { }
    Pair & operator==(const Pair & other) const
    {
        return _0 == other._0 && _1 == other._1;
    }
};

template<typename Comparator>
void bsearch_up(size_t & a, size_t b, Comparator f)
{
    while (a < b)
    {
        size_t avg = (a + b) / 2;
        if (f(avg)) a = avg + 1;
        else        b = avg;
    }
}
template<typename TK, typename TV>
struct PODListMap {
    PODVec<Pair<TK, TV>> list;
    
    constexpr PODListMap() { }
    constexpr PODListMap(std::initializer_list<Pair<TK, TV>> initializer)
    {
        auto first = initializer.begin();
        auto last = initializer.end();
        while (first != last)
        {
            insert(first->_0, first->_1);
            first++;
        }
    }
    
    TV & operator[](const TK & key)
    {
        size_t insert_i = 0;
        bsearch_up(insert_i, list.size(), [&](auto avg) { return list[avg]._0 < key; });
        if (insert_i < list.size() && list[insert_i]._0 == key)
            return list[insert_i]._1;
        
        insert(key, TV{});
        return (*this)[key];
    }
    size_t count(const TK & key)
    {
        size_t insert_i = 0;
        bsearch_up(insert_i, list.size(), [&](auto avg) { return list[avg]._0 < key; });
        return insert_i < list.size() && list[insert_i]._0 == key;
    }
    void clear() { list.clear(); }
    
    
    template<typename U1, typename U2>
    void insert(U1 && key, U2 && val)
    {
        size_t insert_i = 0;
        bsearch_up(insert_i, list.size(), [&](auto avg) { return list[avg]._0 < key; });
        auto pv = Pair<TK, TV>{std::forward<U1>(key), std::forward<U2>(val)};
        if (insert_i == list.size())
            list.push_back(pv);
        else if (list[insert_i]._0 == key)
            list[insert_i] = pv;
        else
            list.insert_at(insert_i, std::move(pv));
    }
    
    auto begin() noexcept { return list.begin(); }
    auto end() noexcept { return list.end(); }
};
