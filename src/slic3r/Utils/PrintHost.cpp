#include "OctoPrint.hpp"
#include "Duet.hpp"

#include <vector>
#include <thread>
#include <boost/optional.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Channel.hpp"

using boost::optional;


namespace Slic3r {


PrintHost::~PrintHost() {}

PrintHost* PrintHost::get_print_host(DynamicPrintConfig *config)
{
    PrintHostType kind = config->option<ConfigOptionEnum<PrintHostType>>("host_type")->value;
    if (kind == htOctoPrint) {
        return new OctoPrint(config);
    } else if (kind == htDuet) {
        return new Duet(config);
    }
    return nullptr;
}


struct PrintHostJobQueue::priv
{
    std::vector<PrintHostJob> jobs;
    Channel<unsigned> channel;

    std::thread bg_thread;
    optional<PrintHostJob> bg_job;
};

PrintHostJobQueue::PrintHostJobQueue()
    : p(new priv())
{
    std::shared_ptr<priv> p2 = p;
    p->bg_thread = std::thread([p2]() {
        // Wait for commands on the channel:
        auto cmd = p2->channel.pop();
        // TODO
    });
}

PrintHostJobQueue::~PrintHostJobQueue()
{
    // TODO: stop the thread
    // if (p && p->bg_thread.joinable()) {
    //     p->bg_thread.detach();
    // }
}


}
