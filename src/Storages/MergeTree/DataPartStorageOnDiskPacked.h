#pragma once
#include <Storages/MergeTree/IDataPartStorage.h>
#include <Storages/MergeTree/DataPartStorageOnDiskBase.h>
#include <IO/PackedFilesIO.h>
#include <IO/PackedFilesReader.h>
#include <mutex>

namespace DB
{

/// A storage for data part that packs all files into a single `data.packed` archive.
/// Reduces S3 object count from O(columns) to O(projections + 1) per part.
///
/// Write path: each writeFile call creates a local temp file. At precommitTransaction,
/// the index is computed from temp file sizes and the archive is streamed to the output.
/// Memory usage: O(index_size + buffer_size), not O(part_size).
class DataPartStorageOnDiskPacked final : public DataPartStorageOnDiskBase
{
public:
    DataPartStorageOnDiskPacked(VolumePtr volume_, std::string root_path_, std::string part_dir_);
    ~DataPartStorageOnDiskPacked() override;

    MergeTreeDataPartStorageType getType() const override { return MergeTreeDataPartStorageType::Packed; }

    MutableDataPartStoragePtr getProjection(const std::string & name, bool use_parent_transaction = true) override; // NOLINT
    DataPartStoragePtr getProjection(const std::string & name) const override;

    bool exists() const override;
    bool existsFile(const std::string & name) const override;
    bool existsDirectory(const std::string & name) const override;

    DataPartStorageIteratorPtr iterate() const override;
    Poco::Timestamp getFileLastModified(const String & file_name) const override;
    size_t getFileSize(const std::string & file_name) const override;
    UInt32 getRefCount(const std::string & file_name) const override;
    std::vector<std::string> getRemotePaths(const std::string & file_name) const override;
    String getUniqueId() const override;

    std::unique_ptr<ReadBufferFromFileBase> readFile(
        const std::string & name,
        const ReadSettings & settings,
        std::optional<size_t> read_hint) const override;

    std::unique_ptr<ReadBufferFromFileBase> readFileIfExists(
        const std::string & name,
        const ReadSettings & settings,
        std::optional<size_t> read_hint) const override;

    void createProjection(const std::string & name) override;

    std::unique_ptr<WriteBufferFromFileBase> writeFile(
        const String & name,
        size_t buf_size,
        WriteMode mode,
        const WriteSettings & settings) override;

    void createFile(const String & name) override;
    void moveFile(const String & from_name, const String & to_name) override;
    void replaceFile(const String & from_name, const String & to_name) override;

    void removeFile(const String & name) override;
    void removeFileIfExists(const String & name) override;

    void createHardLinkFrom(const IDataPartStorage & source, const std::string & from, const std::string & to) override;
    void copyFileFrom(const IDataPartStorage & source, const std::string & from, const std::string & to) override;

    void beginTransaction() override;
    void commitTransaction() override;
    void precommitTransaction() override;
    bool hasActiveTransaction() const override { return transaction != nullptr; }

    static constexpr auto PACKED_FILE_NAME = "data.packed";

private:
    DataPartStorageOnDiskPacked(VolumePtr volume_, std::string root_path_, std::string part_dir_, DiskTransactionPtr transaction_);
    MutableDataPartStoragePtr create(VolumePtr volume_, std::string root_path_, std::string part_dir_, bool initialize_) const override;

    NameSet getActualFileNamesOnDisk(const NameSet &) const override { return {PACKED_FILE_NAME}; }

    std::string getPackedFilePath() const;
    PackedFilesReader & getReader() const;
    void cleanupTempFiles();
    void ensureTempDir();

    /// Read path: lazy-initialized from data.packed on disk.
    mutable std::unique_ptr<PackedFilesReader> reader;
    mutable std::mutex reader_mutex;
    std::optional<PackedFilesIO::Index> preloaded_index;

    /// Write path: temp files on local disk, assembled into archive at precommit.
    String temp_dir_path;
    std::map<String, String> temp_files; /// logical name -> temp file path
    bool need_sync = false;
};

}
