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

#ifndef BRUTEFIR_HPP
#define BRUTEFIR_HPP

#include <cstdio>
#include <memory>
#include <thread>
#include <functional>
#include "common/message/pcm_chunk.hpp"
#include "common/queue.h"
#include <boost/asio.hpp>
#include <boost/process.hpp>

namespace bp = boost::process;
using boost::asio::posix::stream_descriptor;

using add_chunk_callback = std::function<void(std::shared_ptr<msg::PcmChunk>)>;

class BruteFIR
{
public:
    BruteFIR(const std::string& brutefir_config, boost::asio::io_context& ioc, add_chunk_callback cb);
    virtual ~BruteFIR();

    void filter(std::shared_ptr<msg::PcmChunk> chunk);
private:
    Queue<std::shared_ptr<msg::PcmChunk>> chunks_;

    bp::child process_;
    bp::async_pipe pipe_stdout_;
    bp::async_pipe pipe_stdin_;
    bp::async_pipe pipe_stderr_;

    add_chunk_callback add_chunk_;
};

#endif
