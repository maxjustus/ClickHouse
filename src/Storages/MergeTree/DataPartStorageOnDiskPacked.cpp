#include <Storages/MergeTree/DataPartStorageOnDiskPacked.h>

#include <Disks/IDiskTransaction.h>
#include <Disks/SingleDiskVolume.h>
#include <IO/ReadBufferFromFile.h>
#include <IO/ReadBufferFromFileBase.h>
#include <IO/WriteBufferFromFile.h>
#include <IO/WriteBufferFromFileBase.h>
#include <IO/WriteHelpers.h>
#include <IO/copyData.h>
#include <Common/typeid_cast.h>
#include <Common/Exception.h>

namespace fs = std::filesystem;

namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
    extern const int FILE_ALREADY_EXISTS;
}

/// WriteBuffer backed by a local temp file. Defers sync to the final `data.packed` archive.
class TempFileWriteBuffer : public WriteBufferFromFileBase
{
public:
    TempFileWriteBuffer(const String & logical_name_, const String & temp_path_, bool & need_sync_ref_)
        : WriteBufferFromFileBase(0, nullptr, 0)
        , logical_name(logical_name_)
        , impl(std::make_unique<WriteBufferFromFile>(temp_path_))
        , need_sync_ref(need_sync_ref_)
    {
        swap(*impl);
    }

    ~TempFileWriteBuffer() override { swap(*impl); }

    std::string getFileName() const override { return logical_name; }
    void sync() override { need_sync_ref = true; }

    void nextImpl() override { swap(*impl); impl->next(); swap(*impl); }

    void finalizeImpl() override { swap(*impl); impl->finalize(); swap(*impl); }

    void cancelImpl() noexcept override { swap(*impl); impl->cancel(); swap(*impl); }

private:
    const String logical_name;
    std::unique_ptr<WriteBufferFromFile> impl;
    bool & need_sync_ref;
};


DataPartStorageOnDiskPacked::DataPartStorageOnDiskPacked(VolumePtr volume_, std::string root_path_, std::string part_dir_)
    : DataPartStorageOnDiskBase(std::move(volume_), std::move(root_path_), std::move(part_dir_))
{
}

DataPartStorageOnDiskPacked::DataPartStorageOnDiskPacked(
    VolumePtr volume_, std::string root_path_, std::string part_dir_, DiskTransactionPtr transaction_)
    : DataPartStorageOnDiskBase(std::move(volume_), std::move(root_path_), std::move(part_dir_), std::move(transaction_))
{
}

DataPartStorageOnDiskPacked::~DataPartStorageOnDiskPacked()
{
    cleanupTempFiles();
}

MutableDataPartStoragePtr DataPartStorageOnDiskPacked::create(
    VolumePtr volume_, std::string root_path_, std::string part_dir_, bool /*initialize_*/) const
{
    return std::make_shared<DataPartStorageOnDiskPacked>(std::move(volume_), std::move(root_path_), std::move(part_dir_));
}

void DataPartStorageOnDiskPacked::cleanupTempFiles()
{
    for (const auto & [_, temp_path] : temp_files)
        fs::remove(temp_path);
    temp_files.clear();

    if (!temp_dir_path.empty())
        fs::remove(temp_dir_path);
    temp_dir_path.clear();
    need_sync = false;
}

void DataPartStorageOnDiskPacked::ensureTempDir()
{
    if (!temp_dir_path.empty())
        return;
    temp_dir_path = fs::path(volume->getDisk()->getPath()) / root_path / part_dir / "tmp_packed";
    fs::create_directories(temp_dir_path);
}

std::string DataPartStorageOnDiskPacked::getPackedFilePath() const
{
    return fs::path(root_path) / part_dir / PACKED_FILE_NAME;
}

PackedFilesReader & DataPartStorageOnDiskPacked::getReader() const
{
    std::lock_guard lock(reader_mutex);
    if (!reader)
    {
        auto disk = volume->getDisk();
        auto packed_path = getPackedFilePath();

        if (preloaded_index)
            reader = std::make_unique<PackedFilesReader>(disk, packed_path, *preloaded_index);
        else
            reader = std::make_unique<PackedFilesReader>(disk, packed_path, getReadSettings());
    }
    return *reader;
}

MutableDataPartStoragePtr DataPartStorageOnDiskPacked::getProjection(const std::string & name, bool use_parent_transaction) // NOLINT
{
    return std::shared_ptr<DataPartStorageOnDiskPacked>(new DataPartStorageOnDiskPacked(
        volume, std::string(fs::path(root_path) / part_dir), name, use_parent_transaction ? transaction : nullptr));
}

DataPartStoragePtr DataPartStorageOnDiskPacked::getProjection(const std::string & name) const
{
    return std::make_shared<DataPartStorageOnDiskPacked>(volume, std::string(fs::path(root_path) / part_dir), name);
}

bool DataPartStorageOnDiskPacked::exists() const
{
    return volume->getDisk()->existsFile(getPackedFilePath());
}

bool DataPartStorageOnDiskPacked::existsFile(const std::string & name) const
{
    return getReader().exists(name);
}

bool DataPartStorageOnDiskPacked::existsDirectory(const std::string & name) const
{
    return volume->getDisk()->existsDirectory(fs::path(root_path) / part_dir / name);
}


/// Iterator over files inside a packed archive + real directories (projections).
class DataPartStorageIteratorOnDiskPacked final : public IDataPartStorageIterator
{
public:
    DataPartStorageIteratorOnDiskPacked(Names file_names_, DiskPtr disk_, const String & part_path_)
        : disk(std::move(disk_))
        , part_path(part_path_)
    {
        names = std::move(file_names_);
        if (auto dir_it = disk->iterateDirectory(part_path))
        {
            for (; dir_it->isValid(); dir_it->next())
            {
                if (disk->existsDirectory(dir_it->path()))
                    names.push_back(dir_it->name());
            }
        }
    }

    void next() override { ++pos; }
    bool isValid() const override { return pos < names.size(); }
    bool isFile() const override { return isValid() && !disk->existsDirectory(fs::path(part_path) / names[pos]); }
    std::string name() const override { return names[pos]; }
    std::string path() const override { return fs::path(part_path) / names[pos]; }

private:
    DiskPtr disk;
    String part_path;
    Names names;
    size_t pos = 0;
};


DataPartStorageIteratorPtr DataPartStorageOnDiskPacked::iterate() const
{
    return std::make_unique<DataPartStorageIteratorOnDiskPacked>(
        getReader().getFileNames(),
        volume->getDisk(),
        fs::path(root_path) / part_dir);
}

Poco::Timestamp DataPartStorageOnDiskPacked::getFileLastModified(const String & /*file_name*/) const
{
    return volume->getDisk()->getLastModified(getPackedFilePath());
}

size_t DataPartStorageOnDiskPacked::getFileSize(const std::string & file_name) const
{
    return getReader().getFileSize(file_name);
}

UInt32 DataPartStorageOnDiskPacked::getRefCount(const std::string & /*file_name*/) const
{
    return volume->getDisk()->getRefCount(getPackedFilePath());
}

std::vector<std::string> DataPartStorageOnDiskPacked::getRemotePaths(const std::string & /*file_name*/) const
{
    auto objects = volume->getDisk()->getStorageObjects(getPackedFilePath());

    std::vector<std::string> remote_paths;
    remote_paths.reserve(objects.size());

    for (const auto & object : objects)
        remote_paths.push_back(object.remote_path);

    return remote_paths;
}

String DataPartStorageOnDiskPacked::getUniqueId() const
{
    auto disk = volume->getDisk();
    if (!disk->supportZeroCopyReplication())
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Disk {} doesn't support zero-copy replication", disk->getName());

    return disk->getUniqueId(getPackedFilePath());
}

std::unique_ptr<ReadBufferFromFileBase> DataPartStorageOnDiskPacked::readFile(
    const std::string & name,
    const ReadSettings & settings,
    std::optional<size_t> read_hint) const
{
    return getReader().readFile(name, settings, read_hint);
}

std::unique_ptr<ReadBufferFromFileBase> DataPartStorageOnDiskPacked::readFileIfExists(
    const std::string & name,
    const ReadSettings & settings,
    std::optional<size_t> read_hint) const
{
    auto & r = getReader();
    if (!r.exists(name))
        return nullptr;
    return r.readFile(name, settings, read_hint);
}

void DataPartStorageOnDiskPacked::createProjection(const std::string & name)
{
    executeWriteOperation([&](auto & disk) { disk.createDirectory(fs::path(root_path) / part_dir / name); });
}

std::unique_ptr<WriteBufferFromFileBase> DataPartStorageOnDiskPacked::writeFile(
    const String & name,
    size_t /*buf_size*/,
    WriteMode /*mode*/,
    const WriteSettings & /*settings*/)
{
    ensureTempDir();

    auto temp_path = fs::path(temp_dir_path) / ("packed_" + name);

    auto [it, inserted] = temp_files.try_emplace(name, temp_path);
    if (!inserted)
        throw Exception(ErrorCodes::FILE_ALREADY_EXISTS, "File {} already exists in packed part", name);

    return std::make_unique<TempFileWriteBuffer>(name, temp_path, need_sync);
}

void DataPartStorageOnDiskPacked::createFile(const String & /*name*/)
{
    /// No-op: files are created implicitly via writeFile.
}

void DataPartStorageOnDiskPacked::moveFile(const String & from_name, const String & to_name)
{
    if (temp_files.contains(to_name))
        throw Exception(ErrorCodes::FILE_ALREADY_EXISTS,
            "Cannot move file from {} to {}. File {} already exists", from_name, to_name, to_name);

    auto it = temp_files.find(from_name);
    if (it == temp_files.end())
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Cannot move file {}. File does not exist", from_name);

    auto entry = temp_files.extract(it);
    entry.key() = to_name;
    temp_files.insert(std::move(entry));
}

void DataPartStorageOnDiskPacked::replaceFile(const String & from_name, const String & to_name)
{
    auto it = temp_files.find(from_name);
    if (it == temp_files.end())
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Cannot replace file {}. Source does not exist", from_name);

    auto to_it = temp_files.find(to_name);
    if (to_it != temp_files.end())
    {
        fs::remove(to_it->second);
        temp_files.erase(to_it);
    }

    auto entry = temp_files.extract(it);
    entry.key() = to_name;
    temp_files.insert(std::move(entry));
}

void DataPartStorageOnDiskPacked::removeFile(const String & name)
{
    auto it = temp_files.find(name);
    if (it == temp_files.end())
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Cannot remove file {}. File does not exist", name);

    fs::remove(it->second);
    temp_files.erase(it);
}

void DataPartStorageOnDiskPacked::removeFileIfExists(const String & name)
{
    auto it = temp_files.find(name);
    if (it != temp_files.end())
    {
        fs::remove(it->second);
        temp_files.erase(it);
    }
}

void DataPartStorageOnDiskPacked::createHardLinkFrom(const IDataPartStorage & source, const std::string & /*from*/, const std::string & /*to*/)
{
    const auto * source_packed = typeid_cast<const DataPartStorageOnDiskPacked *>(&source);
    if (!source_packed)
        throw Exception(
            ErrorCodes::LOGICAL_ERROR,
            "Cannot create hardlink from different storage type. Expected DataPartStorageOnDiskPacked, got {}",
            typeid(source).name());

    executeWriteOperation([&](auto & disk)
    {
        disk.createHardLink(source_packed->getPackedFilePath(), getPackedFilePath());
    });
}

void DataPartStorageOnDiskPacked::copyFileFrom(const IDataPartStorage & source, const std::string & /*from*/, const std::string & /*to*/)
{
    const auto * source_packed = typeid_cast<const DataPartStorageOnDiskPacked *>(&source);
    if (!source_packed)
        throw Exception(
            ErrorCodes::LOGICAL_ERROR,
            "Cannot copy file from different storage type. Expected DataPartStorageOnDiskPacked, got {}",
            typeid(source).name());

    source_packed->getDisk()->copyFile(
        source_packed->getPackedFilePath(),
        *volume->getDisk(),
        getPackedFilePath(),
        getReadSettings());
}

void DataPartStorageOnDiskPacked::beginTransaction()
{
    if (transaction)
        throw Exception(ErrorCodes::LOGICAL_ERROR,
            "Uncommitted{}transaction already exists", has_shared_transaction ? " shared " : " ");

    transaction = volume->getDisk()->createTransaction();
}

void DataPartStorageOnDiskPacked::precommitTransaction()
{
    if (temp_files.empty())
        return;

    /// Build ordered file list (stable iteration order from std::map).
    std::vector<String> ordered_names;
    ordered_names.reserve(temp_files.size());
    for (const auto & [name, _] : temp_files)
        ordered_names.push_back(name);

    /// Get file sizes from temp files.
    std::vector<UInt64> file_sizes;
    file_sizes.reserve(ordered_names.size());
    for (const auto & name : ordered_names)
        file_sizes.push_back(fs::file_size(temp_files[name]));

    /// Compute index header size: version (1 byte) + num_files (8 bytes) + per-file entries.
    const UInt64 num_files = ordered_names.size();
    UInt64 data_offset = sizeof(PackedFilesIO::VERSION) + sizeof(UInt64);
    for (const auto & name : ordered_names)
        data_offset += getLengthOfVarUInt(name.size()) + name.size() + sizeof(UInt64) * 2;

    /// Open the packed archive — via transaction if available, otherwise directly on disk.
    auto packed_path = getPackedFilePath();
    std::unique_ptr<WriteBufferFromFileBase> out;
    if (transaction)
        out = transaction->writeFile(packed_path, DBMS_DEFAULT_BUFFER_SIZE, WriteMode::Rewrite, {});
    else
        out = volume->getDisk()->writeFile(packed_path, DBMS_DEFAULT_BUFFER_SIZE, WriteMode::Rewrite, {});

    /// Write header: version + num_files.
    writeIntBinary(PackedFilesIO::VERSION, *out);
    writeIntBinary(num_files, *out);

    /// Write index entries and build the in-memory index.
    PackedFilesIO::Index index;

    for (size_t i = 0; i < ordered_names.size(); ++i)
    {
        const auto & name = ordered_names[i];
        const UInt64 size = file_sizes[i];

        writeStringBinary(name, *out);
        writeIntBinary(data_offset, *out);
        writeIntBinary(size, *out);

        index[name] = {data_offset, size};
        data_offset += size;
    }

    /// Stream each temp file's contents to the archive.
    for (const auto & name : ordered_names)
    {
        ReadBufferFromFile in(temp_files[name]);
        copyData(in, *out);
    }

    if (need_sync)
        out->sync();

    out->finalize();

    /// Clean up temp files; store index for the reader.
    cleanupTempFiles();

    std::lock_guard lock(reader_mutex);
    preloaded_index = std::move(index);
}

void DataPartStorageOnDiskPacked::commitTransaction()
{
    if (!transaction)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "There is no uncommitted transaction");

    if (has_shared_transaction)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Cannot commit shared transaction");

    transaction->commit();
    transaction.reset();

    /// Reset reader so it re-initializes with the preloaded index on next access.
    std::lock_guard lock(reader_mutex);
    reader.reset();
}

}
