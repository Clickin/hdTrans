#include "hdTrans/dict/dict.h"

namespace hdTrans::dict {

namespace {

uint64_t Hash(std::string_view s, uint64_t h) {
    for (const char c : s) h = (h ^ static_cast<unsigned char>(c)) * 0x100000001B3ull;
    return (h ^ 0xFF) * 0x100000001B3ull;  // field separator
}

uint64_t HashCodes(std::string_view part, std::string_view sem, std::string_view gram, std::string_view dict) {
    return Hash(dict, Hash(gram, Hash(sem, Hash(part, 0xCBF29CE484222325ull))));
}

uint64_t ReadUvarint(std::string_view s, std::size_t& i) {
    uint64_t x = 0;
    for (int shift = 0;; shift += 7) {
        const auto b = static_cast<unsigned char>(s[i++]);
        if (b < 0x80) return x | uint64_t{b} << shift;
        x |= uint64_t{b & 0x7Fu} << shift;
    }
}

}  // namespace

int32_t CodeTable::Intern(std::string_view part, std::string_view sem, std::string_view gram, std::string_view dict) {
    if (slots_.empty() || list_.size() * 2 >= slots_.size()) Grow();
    const std::size_t mask = slots_.size() - 1;
    for (std::size_t s = HashCodes(part, sem, gram, dict) & mask;; s = (s + 1) & mask) {
        const int32_t i = slots_[s];
        if (i < 0) {
            Codes& c = list_.emplace_back();
            c.part = PartCode(part);
            c.partText = part;
            c.sem = SemCodes(sem);
            c.gram = GramCodes(gram);
            c.dict = DictCode(dict);
            slots_[s] = static_cast<int32_t>(list_.size() - 1);
            return slots_[s];
        }
        const Codes& c = list_[static_cast<std::size_t>(i)];
        if (c.partText == part && c.sem.Text() == sem && c.gram.Text() == gram && c.dict.Text() == dict) return i;
    }
}

void CodeTable::Grow() {
    std::size_t size = slots_.empty() ? 1024 : slots_.size() * 2;
    slots_.assign(size, -1);
    for (std::size_t i = 0; i < list_.size(); i++) {
        const Codes& c = list_[i];
        for (std::size_t s = HashCodes(c.partText, c.sem.Text(), c.gram.Text(), c.dict.Text()) & (size - 1);;
             s = (s + 1) & (size - 1)) {
            if (slots_[s] < 0) {
                slots_[s] = static_cast<int32_t>(i);
                break;
            }
        }
    }
}

void CodeTable::SetWithXXX(int32_t i) {
    Codes& c = list_[static_cast<std::size_t>(i)];
    if (c.withXXX) return;
    if (c.sem.Contains("XXX")) {
        c.withXXX = &c;
        return;
    }
    const std::string sem = std::string(c.sem.Text()) + "XXX";
    const std::string part = c.partText;
    const std::string gram(c.gram.Text());
    const std::string dict(c.dict.Text());
    const int32_t twin = Intern(part, sem, gram, dict);
    list_[static_cast<std::size_t>(twin)].withXXX = At(twin);
    list_[static_cast<std::size_t>(i)].withXXX = At(twin);
}

void CodeTable::SetAsPlain(int32_t i) {
    Codes& c = list_[static_cast<std::size_t>(i)];
    if (c.asPlain) return;
    if (c.dict == "GN") {
        c.asPlain = &c;
        return;
    }
    const std::string part = c.partText;
    const std::string sem(c.sem.Text());
    const std::string gram(c.gram.Text());
    const int32_t twin = Intern(part, sem, gram, "GN");
    list_[static_cast<std::size_t>(twin)].asPlain = At(twin);
    list_[static_cast<std::size_t>(i)].asPlain = At(twin);
}

std::size_t CodeTable::MemoryBytes() const {
    std::size_t n = slots_.size() * sizeof(int32_t) + list_.size() * sizeof(Codes);
    for (const Codes& c : list_) {
        for (const std::string_view s : {std::string_view(c.partText), c.sem.Text(), c.gram.Text(), c.dict.Text()}) {
            if (s.size() > sizeof(std::string) - 2) n += s.size() + 1;
        }
    }
    return n;
}

std::size_t Dict::RecordsStart(int k) const {
    std::size_t i = starts_[static_cast<std::size_t>(k / kKeysPerStart)];
    for (int skip = k % kKeysPerStart; skip > 0;) {
        const uint64_t h = ReadUvarint(blob_, i);
        i += static_cast<std::size_t>(h >> 2);
        for (int fields = (h & 2) ? 3 : 2; fields > 0; fields--) {
            while (static_cast<unsigned char>(blob_[i]) >= 0x80) i++;
            i++;
        }
        if ((h & 1) == 0) skip--;
    }
    return i;
}

bool Dict::Decode(std::size_t& i, Record& r) const {
    const uint64_t h = ReadUvarint(blob_, i);
    const auto n = static_cast<std::size_t>(h >> 2);
    r.target = std::string_view(blob_).substr(i, n);
    i += n;
    r.codes = codes_[static_cast<std::size_t>(ReadUvarint(blob_, i))];
    r.rule = (h & 2) ? static_cast<int32_t>(static_cast<uint32_t>(ReadUvarint(blob_, i))) : 0;
    r.id = static_cast<int32_t>(static_cast<uint32_t>(ReadUvarint(blob_, i)));
    return (h & 1) != 0;
}

void Dict::AppendRecords(std::vector<Record>& out, int k) const {
    EachRecord(k, [&](const Record& r) {
        out.push_back(r);
        return true;
    });
}

std::vector<Record> Dict::Records(std::string_view key) const {
    std::vector<Record> out;
    if (const int k = keys_.Find(key); k >= 0) AppendRecords(out, k);
    return out;
}

Dict Dict::FromView(ViewData data) {
    Dict d;
    d.keys_ = Trie::FromView(data.keys);
    d.blob_ = data.blob;
    d.starts_ = data.starts;
    d.codes_ = data.codes;
    return d;
}

std::size_t Dict::MemoryBytes() const {
    if (storage_) {
        return keys_.MemoryBytes() + storage_->blob.capacity() + storage_->starts.capacity() * sizeof(uint32_t) +
               storage_->codes.capacity() * sizeof(const Codes*);
    }
    return keys_.MemoryBytes() + blob_.size() + starts_.size_bytes() + codes_.size_bytes();
}

}  // namespace hdTrans::dict
