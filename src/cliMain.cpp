#include "popl.hpp"
#include <iostream>
#include "rotators/CamPTZ.hpp"
#include "rotators/rotctld.hpp"
#include "RotatorCommon.hpp"

// TODO: split pipeline
int main(int argc, char *argv[]) {
  popl::OptionParser op("Allowed options");
  auto helpOption   = op.add<popl::Switch>("h", "help", "produce help message");
  auto srcTcpHost = op.add<popl::Implicit<std::string>>("", "rotctld-tcp-host", "TCP host to bind for source", "0.0.0.0");
  auto srcTcpPort = op.add<popl::Implicit<int>>("", "rotctld-tcp-port", "TCP port to bind for source", 4533);
  auto sinkTcpHost = op.add<popl::Implicit<std::string>>("", "rotator-tcp-host", "TCP host of rotator", "192.168.3.136");
  auto sinkTcpPort  = op.add<popl::Implicit<int>>("", "rotator-tcp-port", "TCP port of rotator", 4196);
  auto disablePresetReset = op.add<popl::Switch>("", "disable-preset-reset", "Disable preset reset");
  auto disableGpredictWalkaround = op.add<popl::Switch>("", "disable-workaround-for-gpredict", "Disable Gpredict walkaround");
  auto sinkAziOffset  = op.add<popl::Implicit<double>>("", "sink-azi-offset", "azi offset of rotator", -9.0);
  auto sinkEleOffset  = op.add<popl::Implicit<double>>("", "sink-ele-offset", "ele offset of rotator", 0.0);
  auto disableSmartSink = op.add<popl::Switch>("", "disable-smart-sink", "Disable smart sink");
  auto disableSinkKeepAlive = op.add<popl::Switch>("", "disable-sink-keepalive", "Disable sink keepalive (5sec rotate cmd autoreplay)");

  op.parse(argc, argv);

  std::cout << "Rotator Bridge for BY6DX; GitHub https://github.com/BY6DX/Rotator-Bridge" << std::endl;

  // print auto-generated help message
  if (helpOption->is_set()) {
    std::cout << op << "\n";
    return 0;
  }

  SOCKET_INIT();

  auto source = rotctld();
  source.Initialize(srcTcpHost->value(), srcTcpPort->value(), !disableGpredictWalkaround->is_set());

  auto sink = CamPTZ();
  sink.Initialize(
    sinkTcpHost->value(), sinkTcpPort->value(), sinkAziOffset->value(), sinkEleOffset->value(),
    !disableSmartSink->value(),
    !disableSinkKeepAlive->value()
  );

  source.SetRequestHandler([&](RotatorRequest req) -> RotatorResponse {
    // Visualize
    if (req.cmd == CHANGE_AZI) {
      printf("[Pipeline] Requested new Azi change, newAzi=%lf\n", req.payload.ChangeAzi.aziRequested);
    } else if (req.cmd == CHANGE_ELE) {
      printf("[Pipeline] Requested new Ele change, newEle=%lf\n", req.payload.ChangeEle.eleRequested);
    }

    auto ret = sink.RequestSync(req, 1000);
    if (!ret.has_value()) {
      fprintf(stderr, "main: Error while processing request\n");
      RotatorResponse resp;
      resp.success = false;

      return resp;
    }

    return ret.value();
  });
  
  sink.Start();
  source.Start();

  if (!disablePresetReset->is_set()) {
    // disable power-on self test
    RotatorRequest req;
    req.cmd = CAMPTZ_PRESET_CLEAR;
    req.payload.CamPTZPreset.presetIdx = 156;
    sink.Request(req, [](RotatorResponse resp) {
      if (resp.success) {
        printf("[Pipeline] Power-on self test for CamPTZ disabled.\n");
      } else {
        printf("[Pipeline] ERR while disabling Power-on self test for CamPTZ.\n");
      }
    });

    // disable automatic zero-returning
    req.payload.CamPTZPreset.presetIdx = 130;
    sink.Request(req, [](RotatorResponse resp) {
      if (resp.success) {
        printf("[Pipeline] Automatic zero-returning for CamPTZ disabled.\n");
      } else {
        printf("[Pipeline] ERR while disabling automatic zero-returning for CamPTZ.\n");
      }
    });
  }

  source.WaitForClose();
  
  SOCKET_EXIT();
  return 0;
}