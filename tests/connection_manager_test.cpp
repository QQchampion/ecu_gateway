#include "gateway/ConnectionManager.h"

#include <cassert>
#include <cstdint>

int main()
{
    using message::DeviceId;
    ConnectionManager manager;
    assert(!manager.add_connection(-1));
    assert(manager.find_by_fd(-1) == nullptr);
    assert(manager.add_connection(5));
    assert(manager.find_by_fd(5) != nullptr);
    assert(!manager.find_by_fd(5)->device_id);
    assert(manager.find_by_fd(5)->send_queue.empty());
    assert(manager.find_by_fd(5)->send_offset == 0);
    assert(!manager.add_connection(5));
    assert(manager.find_by_fd(99) == nullptr);
    assert(!manager.register_device(99, DeviceId::POWER_ECU));
    assert(manager.register_device(5, DeviceId::POWER_ECU));
    assert(manager.find_by_fd(5)->device_id == DeviceId::POWER_ECU);
    assert(manager.find_fd_by_device(DeviceId::POWER_ECU) == 5);
    assert(!manager.register_device(5, DeviceId::BCM_ECU));
    assert(manager.find_by_fd(5)->device_id == DeviceId::POWER_ECU);
    assert(!manager.find_fd_by_device(DeviceId::BCM_ECU));

    assert(manager.add_connection(6));
    assert(!manager.register_device(6, DeviceId::POWER_ECU));
    assert(!manager.find_by_fd(6)->device_id);
    assert(manager.find_fd_by_device(DeviceId::POWER_ECU) == 5);
    assert(!manager.register_device(6, DeviceId::GATEWAY));
    assert(!manager.register_device(6, DeviceId::BCM_ECU));
    assert(!manager.find_by_fd(6)->device_id);
    assert(!manager.find_fd_by_device(DeviceId::BCM_ECU));
    assert(!manager.register_device(6, static_cast<DeviceId>(0)));
    assert(!manager.register_device(6, static_cast<DeviceId>(255)));
    assert(!manager.find_fd_by_device(DeviceId::GATEWAY));
    assert(manager.register_device(6, DeviceId::CLUSTER_ECU));
    assert(manager.find_fd_by_device(DeviceId::CLUSTER_ECU) == 6);

    assert(manager.add_connection(7));
    assert(manager.remove_connection(7));
    assert(manager.find_by_fd(7) == nullptr);
    assert(!manager.remove_connection(7));
    assert(manager.find_fd_by_device(DeviceId::POWER_ECU) == 5);
    assert(manager.find_fd_by_device(DeviceId::CLUSTER_ECU) == 6);

    assert(manager.remove_connection(5));
    assert(manager.find_by_fd(5) == nullptr);
    assert(!manager.find_fd_by_device(DeviceId::POWER_ECU));
    assert(manager.find_fd_by_device(DeviceId::CLUSTER_ECU) == 6);
    assert(manager.add_connection(5));
    assert(manager.register_device(5, DeviceId::POWER_ECU));
    assert(manager.remove_connection(6));
    assert(!manager.find_fd_by_device(DeviceId::CLUSTER_ECU));
    assert(manager.find_fd_by_device(DeviceId::POWER_ECU) == 5);
    assert(manager.register_device(6, DeviceId::BCM_ECU) == false);
    return 0;
}
