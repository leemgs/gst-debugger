/*
 * controller.h
 *
 *  Created on: Jul 22, 2015
 *      Author: loganek
 */

#ifndef SRC_GST_DEBUGGER_CONTROLLER_CONTROLLER_H_
#define SRC_GST_DEBUGGER_CONTROLLER_CONTROLLER_H_

#include "command_factory.h"
#include "iview.h"
#include "connection_controller.h"
#include "topology_controller.h"

#include "protocol/gstdebugger.pb.h"

class Controller :
		public std::enable_shared_from_this<Controller>,
		public CommandFactory,
		public ConnectionController,
		public TopologyController
{
private:
	IMainView *view;

	void process_frame(const GstreamerInfo& info);

public:
	Controller(IMainView *view);

	void send_command(const Command& cmd);

	int run(int &argc, char **&argv);

	void model_up();
	void model_down(const std::string &name);

	sigc::signal<void, const GstreamerLog&> on_log_received;
	sigc::signal<void, const DebugCategoryList&> on_debug_categories_received;
};

#endif /* SRC_GST_DEBUGGER_CONTROLLER_CONTROLLER_H_ */