/***
    This file is part of snapcast
    Copyright (C) 2022 Michael Auchter

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

#include <string>
#include <cstdio>
#include <vector>
#include <boost/process/start_dir.hpp>
#include <boost/filesystem.hpp>
#include "brutefir.hpp"
#include "common/aixlog.hpp"
#include "common/snap_exception.hpp"
#include "common/str_compat.hpp"
#include "common/utils.hpp"
#include "common/utils/string_utils.hpp"

using namespace std;

static constexpr auto LOG_TAG = "BruteFIR";

BruteFIR::BruteFIR(const std::string& brutefir_config, boost::asio::io_context& ioc, add_chunk_callback cb)
    : pipe_stdout_(ioc), pipe_stdin_(ioc), pipe_stderr_(ioc), add_chunk_(cb)
{
    auto exe = bp::search_path("brutefir");
    if (exe == "")
        throw SnapException("brutefir not found");

    auto config = boost::filesystem::path(brutefir_config);

    LOG(DEBUG, LOG_TAG) << "Found BruteFIR binary at: " << exe << "\n";
    LOG(DEBUG, LOG_TAG) << "Using config: " << brutefir_config << "\n";
    LOG(DEBUG, LOG_TAG) << "Parent: " << config.parent_path() << "\n";

    try
    {
        bp::system("killall brutefir");
    }
    catch (const std::exception &)
    {
        try
        {
            bp::system("pkill brutefir");
        }
        catch (const std::exception &)
        {
            LOG(WARNING, LOG_TAG) << "Failed to kill existing BruteFIR process" << "\n";
        }
    }

    process_ = bp::child(exe, brutefir_config, bp::start_dir(config.parent_path()), bp::std_out > pipe_stdout_, bp::std_err > pipe_stderr_, bp::std_in < pipe_stdin_);
}

BruteFIR::~BruteFIR()
{
    if (process_.running())
    {
        LOG(DEBUG, LOG_TAG) << "Killing BruteFIR process" << "\n";
        ::kill(-process_.native_handle(), SIGINT);
    }
}

void BruteFIR::filter(std::shared_ptr<msg::PcmChunk> chunk)
{
    if (!process_.running())
    {
        throw SnapException("BruteFIR process not running, bailing...");
    }

    boost::asio::async_write(pipe_stdin_, boost::asio::buffer(chunk->payload, chunk->payloadSize), [this](const std::error_code& ec, std::size_t) {
        if (ec)
        {
            LOG(WARNING, LOG_TAG) << "async write failed: " << ec.message() << "\n";
            return;
        }
    });

    chunks_.push(chunk);

    boost::asio::async_read(pipe_stdout_, boost::asio::buffer(chunk->payload, chunk->payloadSize), [this](const std::error_code& ec, std::size_t) {
        if (ec)
        {
            LOG(WARNING, LOG_TAG) << "async read failed: " << ec.message() << "\n";
            return;
        }

        auto chunk = chunks_.pop();
        add_chunk_(chunk);
    });

}

