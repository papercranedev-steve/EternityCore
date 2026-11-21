#include "Companions/Talk/TalkService.h"
#include "Companions/Talk/Services/ServiceList.h"

namespace Companions
{
namespace Talk
{
    std::vector<std::unique_ptr<Service>> const& Services()
    {
        // The window lists them in this order.
        static std::vector<std::unique_ptr<Service>> const services = []()
        {
            std::vector<std::unique_ptr<Service>> list;
            list.push_back(MakeStayService());
            list.push_back(MakeBagsService());
            list.push_back(MakeCraftService());
            return list;
        }();
        return services;
    }
}
}
