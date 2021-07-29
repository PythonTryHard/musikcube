//////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2004-2021 musikcube team
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the author nor the names of other contributors may
//      be used to endorse or promote products derived from this software
//      without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////////

#include "pch.hpp"
#include <musikcore/library/LocalLibraryConstants.h>
#include <musikcore/library/query/util/Serialization.h>
#include <musikcore/i18n/Locale.h>
#include "DirectoryTrackListQuery.h"
#include "CategoryTrackListQuery.h"

using musik::core::db::Statement;
using musik::core::db::Row;
using musik::core::ILibraryPtr;

using namespace musik::core::db;
using namespace musik::core::library::query;
using namespace musik::core::library::query::serialization;
using namespace musik::core::library;

const std::string DirectoryTrackListQuery::kQueryName = "DirectoryTrackListQuery";

DirectoryTrackListQuery::DirectoryTrackListQuery(
    ILibraryPtr library,
    const std::string& directory,
    const std::string& filter)
{
    this->library = library;
    this->directory = directory;
    this->filter = filter;
    this->result = std::make_shared<TrackList>(library);
    this->headers = std::make_shared<std::set<size_t>>();
    this->durations = std::make_shared<std::map<size_t, size_t>>();
    this->hash = std::hash<std::string>()(directory + "-" + filter);
}

bool DirectoryTrackListQuery::OnRun(Connection& db) {
    this->result = std::make_shared<TrackList>(library);
    this->headers = std::make_shared<std::set<size_t>>();
    this->durations = std::make_shared<std::map<size_t, size_t>>();

    std::string query =
        " SELECT t.id, t.duration, al.name "
        " FROM tracks t, albums al, artists ar, genres gn "
        " WHERE t.visible=1 AND directory_id IN ("
        "   SELECT id FROM directories WHERE name LIKE ?)"
        " AND t.album_id=al.id AND t.visual_genre_id=gn.id AND t.visual_artist_id=ar.id "
        " ORDER BY al.name, disc, track, ar.name ";

    query += this->GetLimitAndOffset();

    Statement select(query.c_str(), db);
    select.BindText(0, this->directory + "%");

    std::string lastAlbum;
    size_t lastHeaderIndex = 0;
    size_t trackDuration = 0;
    size_t runningDuration = 0;
    size_t index = 0;

    while (select.Step() == db::Row) {
        const int64_t id = select.ColumnInt64(0);
        trackDuration = select.ColumnInt32(1);
        std::string album = select.ColumnText(2);

        if (!album.size()) {
            album = _TSTR("tracklist_unknown_album");
        }

        if (album != lastAlbum) {
            if (!headers->empty()) { /* @copypaste */
                (*durations)[lastHeaderIndex] = runningDuration;
                lastHeaderIndex = index;
                runningDuration = 0;
            }
            headers->insert(index);
            lastAlbum = album;
        }

        runningDuration += trackDuration;

        result->Add(id);
        ++index;
    }

    if (!headers->empty()) {
        (*durations)[lastHeaderIndex] = runningDuration;
    }

    return true;
}

/* ISerializableQuery */

std::string DirectoryTrackListQuery::SerializeQuery() {
    nlohmann::json output = {
        { "name", kQueryName },
        { "options", {
            { "directory", directory },
            { "filter", filter },
        }}
    };
    return FinalizeSerializedQueryWithLimitAndOffset(output);
}

std::string DirectoryTrackListQuery::SerializeResult() {
    return InitializeSerializedResultWithHeadersAndTrackList().dump();
}

void DirectoryTrackListQuery::DeserializeResult(const std::string& data) {
    this->SetStatus(IQuery::Failed);
    nlohmann::json result = nlohmann::json::parse(data)["result"];
    this->DeserializeTrackListAndHeaders(result, this->library, this);
    this->SetStatus(IQuery::Finished);
}

std::shared_ptr<DirectoryTrackListQuery> DirectoryTrackListQuery::DeserializeQuery(
    ILibraryPtr library, const std::string& data)
{
    auto options = nlohmann::json::parse(data)["options"];
    auto result = std::make_shared<DirectoryTrackListQuery>(
        library,
        options["directory"].get<std::string>(),
        options["filter"].get<std::string>());
    result->ExtractLimitAndOffsetFromDeserializedQuery(options);
    return result;
}
