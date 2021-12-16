/***
    This file is part of snapcast
    Copyright (C) 2014-2021  Johannes Pohl

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
***/

#include "librespot_stream.hpp"
#include "common/aixlog.hpp"
#include "common/snap_exception.hpp"
#include "common/utils.hpp"
#include "common/utils/string_utils.hpp"
#include <regex>


using namespace std;

namespace streamreader
{

static constexpr auto LOG_TAG = "LibrespotStream";


LibrespotStream::LibrespotStream(PcmListener* pcmListener, boost::asio::io_context& ioc, const StreamUri& uri) : ProcessStream(pcmListener, ioc, uri)
{
    wd_timeout_sec_ = cpt::stoul(uri_.getQuery("wd_timeout", "7800")); ///< 130min

    string username = uri_.getQuery("username", "");
    string password = uri_.getQuery("password", "");
    string cache = uri_.getQuery("cache", "");
    bool disable_audio_cache = (uri_.getQuery("disable_audio_cache", "false") == "true");
    string volume = uri_.getQuery("volume", "100");
    string volume_ctrl = uri_.getQuery("volume_ctrl", "");
    string bitrate = uri_.getQuery("bitrate", "320");
    string devicename = uri_.getQuery("devicename", "Snapcast");
    string onevent = uri_.getQuery("onevent", "");
    bool normalize = (uri_.getQuery("normalize", "false") == "true");
    bool autoplay = (uri_.getQuery("autoplay", "false") == "true");
    killall_ = (uri_.getQuery("killall", "false") == "true");

    if (username.empty() != password.empty())
        throw SnapException(R"(missing parameter "username" or "password" (must provide both, or neither))");

    if (!params_.empty())
        params_ += " ";
    params_ += "--name \"" + devicename + "\"";
    if (!username.empty() && !password.empty())
        params_ += " --username \"" + username + "\" --password \"" + password + "\"";
    params_ += " --bitrate " + bitrate + " --backend pipe";
    if (!cache.empty())
        params_ += " --cache \"" + cache + "\"";
    if (disable_audio_cache)
        params_ += " --disable-audio-cache";
    if (!volume.empty())
        params_ += " --initial-volume " + volume;
    if (!volume_ctrl.empty())
        params_ += " --volume-ctrl " + volume_ctrl;
    if (!onevent.empty())
        params_ += " --onevent \"" + onevent + "\"";
    if (normalize)
        params_ += " --enable-volume-normalisation";
    if (autoplay)
        params_ += " --autoplay";
    params_ += " --verbose";

    if (uri_.query.find("username") != uri_.query.end())
        uri_.query["username"] = "xxx";
    if (uri_.query.find("password") != uri_.query.end())
        uri_.query["password"] = "xxx";
    //	LOG(INFO, LOG_TAG) << "params: " << params << "\n";
}


void LibrespotStream::initExeAndPath(const std::string& filename)
{
    path_ = "";
    exe_ = findExe(filename);
    if (!fileExists(exe_) || (exe_ == "/"))
    {
        exe_ = findExe("librespot");
        if (!fileExists(exe_))
            throw SnapException("librespot not found");
    }

    if (exe_.find('/') != string::npos)
    {
        path_ = exe_.substr(0, exe_.find_last_of('/') + 1);
        exe_ = exe_.substr(exe_.find_last_of('/') + 1);
    }

    if (killall_)
    {
        /// kill if it's already running
        execGetOutput("killall " + exe_);
    }
}


void LibrespotStream::onStderrMsg(const std::string& line)
{
    static bool libreelec_patched = false;
    // Watch stderr for 'Loading track' messages and set the stream metadata
    // For more than track name check: https://github.com/plietar/librespot/issues/154

    /// Watch will kill librespot if there was no message received for 130min
    // 2021-05-09 09-25-48.651 [Info] (LibrespotStream) (Spotify) [2021-05-09T07:25:48Z DEBUG librespot_playback::player] command=Load(SpotifyId
    // 2021-05-09 09-25-48.651 [Info] (LibrespotStream) (Spotify) [2021-05-09T07:25:48Z TRACE librespot_connect::spirc] Sending status to server
    // 2021-05-09 09-25-48.746 [Info] (LibrespotStream) (Spotify) [2021-05-09T07:25:48Z WARN  librespot_connect::spirc] No autoplay_uri found
    // 2021-05-09 09-25-48.747 [Info] (LibrespotStream) (Spotify) [2021-05-09T07:25:48Z ERROR librespot_connect::spirc] AutoplayError: MercuryError
    // 2021-05-09 09-25-48.750 [Info] (LibrespotStream) (Spotify) [2021-05-09T07:25:48Z INFO  librespot_playback::player] Loading <Big Gangsta>

    // Parse log level, source and message from the log line
    // Format: [2021-05-09T08:31:08Z DEBUG librespot_playback::player] new Player[0]
    std::string level;
    std::string source;
    std::string message;
    level = utils::string::split_left(line, ' ', source);
    level = utils::string::split_left(source, ' ', source);
    source = utils::string::split_left(source, ']', message);
    utils::string::trim(level);
    utils::string::trim(source);
    utils::string::trim(message);

    AixLog::Severity severity = AixLog::Severity::info;
    bool parsed = true;
    if (level == "TRACE")
        severity = AixLog::Severity::trace;
    else if (level == "DEBUG")
        severity = AixLog::Severity::debug;
    else if (level == "INFO")
        severity = AixLog::Severity::info;
    else if (level == "WARN")
        severity = AixLog::Severity::warning;
    else if (level == "ERROR")
        severity = AixLog::Severity::error;
    else
        parsed = false;

    if (parsed)
        LOG(severity, source) << message << "\n";
    else
        LOG(INFO, LOG_TAG) << "(" << getName() << ") " << line << "\n";

    // Librespot patch:
    // 	info!("metadata:{{\"ARTIST\":\"{}\",\"TITLE\":\"{}\"}}", artist.name, track.name);
    // non patched:
    // 	info!("Track \"{}\" loaded", track.name);

    // If we detect a patched libreelec we don't want to bother with this anymoer
    // to avoid duplicate metadata pushes
    smatch m;
    if (!libreelec_patched)
    {
        static regex re_nonpatched("Track \"(.*)\" loaded");
        if (regex_search(line, m, re_nonpatched))
        {
            LOG(INFO, LOG_TAG) << "metadata: <" << m[1] << ">\n";

            json jtag = {{"TITLE", string(m[1])}};
            setMeta(jtag);
        }
    }

    // Parse the patched version
    static regex re_patched("metadata:(.*)");
    if (regex_search(line, m, re_patched))
    {
        LOG(INFO, LOG_TAG) << "metadata: <" << m[1] << ">\n";

        setMeta(json::parse(m[1].str()));
        libreelec_patched = true;
    }
}

} // namespace streamreader
