//
//  C4DatabaseInternal.hh
//  CBForest
//
//  Created by Jens Alfke on 8/12/16.
//  Copyright © 2016 Couchbase. All rights reserved.
//

#ifndef c4DatabaseInternal_hh
#define c4DatabaseInternal_hh

#include "c4Internal.hh"
#include "c4Database.h"
#include "c4Document.h"
#include "CASRevisionStore.hh"

namespace c4Internal {
    class C4DocumentInternal;
}


// c4Database must be in the global namespace because it's forward-declared in the C API.

struct c4Database : public RefCounted<c4Database> {

    c4Database(std::string path,
               const C4DatabaseConfig &config);

    static Database* newDatabase(std::string path,
                                 const C4DatabaseConfig &config,
                                 bool isMainDB);

    Database* db()                                      {return _db.get();}

    const C4DatabaseConfig config;

    // The database format/schema -- 1 for Couchbase Lite 1.x, 2 for CBL 2
    const uint8_t schema()          {return (config.flags & kC4DB_V2Format) ? 2 : 1;}

    bool mustBeSchema(int schema, C4Error*);

    Transaction& transaction() {
        CBFAssert(_transaction);
        return *_transaction;
    }

    // Transaction methods below acquire _transactionMutex. Do not call them if
    // _mutex is already locked, or deadlock may occur!
    void beginTransaction();
    bool inTransaction();
    bool mustBeInTransaction(C4Error *outError);
    bool mustNotBeInTransaction(C4Error *outError);
    bool endTransaction(bool commit);

    KeyStore& defaultKeyStore()                         {return _db->defaultKeyStore();}
    KeyStore& getKeyStore(const string &name) const     {return _db->getKeyStore(name);}

    virtual C4DocumentInternal* newDocumentInstance(C4Slice docID) =0;
    virtual C4DocumentInternal* newDocumentInstance(const Document&) =0;
    virtual bool readDocMeta(const Document&,
                             C4DocumentFlags*,
                             alloc_slice *revID =nullptr,
                             slice *docType =nullptr) =0;

    static bool rekey(Database* database, const C4EncryptionKey *newKey, C4Error *outError);

#if C4DB_THREADSAFE
    // Mutex for synchronizing Database calls. Non-recursive!
    std::mutex _mutex;
#endif

protected:
    virtual ~c4Database() { CBFAssert(_transactionLevel == 0); }

private:
    std::unique_ptr<Database>   _db;                    // Underlying Database
    Transaction*                _transaction {NULL};    // Current Transaction, or null
    int                         _transactionLevel {0};  // Nesting level of transaction
#if C4DB_THREADSAFE
    // Recursive mutex for accessing _transaction and _transactionLevel.
    // Must be acquired BEFORE _mutex, or deadlock may occur!
    std::recursive_mutex        _transactionMutex;
#endif
};


namespace c4Internal {

    // Subclass for old (rev-tree) schema
    class c4DatabaseV1 : public c4Database {
    public:
        c4DatabaseV1(std::string path, const C4DatabaseConfig &config)
        :c4Database(path, config)
        { }
        C4DocumentInternal* newDocumentInstance(C4Slice docID) override;
        C4DocumentInternal* newDocumentInstance(const Document&) override;
        bool readDocMeta(const Document&,
                         C4DocumentFlags*,
                         alloc_slice *revID =nullptr,
                         slice *docType =nullptr) override;
    };


    // Subclass for new (version-vector) schema
    class c4DatabaseV2 : public c4Database {
    public:
        c4DatabaseV2(std::string path, const C4DatabaseConfig &config)
        :c4Database(path, config)
        { }

        C4DocumentInternal* newDocumentInstance(C4Slice docID) override;
        C4DocumentInternal* newDocumentInstance(const Document&) override;

        CASRevisionStore& revisionStore();

        bool readDocMeta(const Document&,
                         C4DocumentFlags*,
                         alloc_slice *revID =nullptr,
                         slice *docType =nullptr) override;

    private:
        std::unique_ptr<CASRevisionStore> _revisionStore;
    };

}


#if C4DB_THREADSAFE
#define WITH_LOCK(db) std::lock_guard<std::mutex> _lock((db)->_mutex)
#else
#define WITH_LOCK(db) do { } while (0)  // no-op
#endif


#endif /* c4DatabaseInternal_hh */