// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/api.hpp"
#include <chrono>
#include <thread>
int main(int argc,char **argv) {
  if(argc!=2) return 2;
  yolo::DashboardView v;
  v.state="MINING";v.elapsed=60;v.accepted=7;v.rejected=1;v.stale=2;v.pending=4;
  yolo::DeviceView gpu;gpu.label="GPU2";gpu.name="Test GPU";gpu.pci_bus="0000:0a:00.0";
  gpu.hashes_per_second=2000000;gpu.accepted=6;gpu.sensors.temperature=62;gpu.sensors.fan_percent=45;
  yolo::DeviceView cpu;cpu.label="CPU";cpu.hashes_per_second=1000000;cpu.accepted=1;
  v.devices={gpu,cpu};
  yolo::ApiServer api(std::stoul(argv[1]));api.update(v);
  std::this_thread::sleep_for(std::chrono::seconds(8));
}
