/*
 * controller.cpp
 *
 *  Created on: Jul 22, 2015
 *      Author: loganek
 */

#include "controller.h"
#include "ui_utils.h"
#include "element_path_processor.h"

#include "gvalue-converter/gvalue_enum.h"
#include "gvalue-converter/gvalue_flags.h"

#include "common/serializer.h"
#include "common/common.h"

#include <gtkmm.h>

#include <iostream>

Controller::Controller(IMainView *view)
 : view(view)
{
	client->signal_frame_received.connect(sigc::mem_fun(*this, &Controller::process_frame));
	client->signal_status_changed.connect([this](bool connected) {
		if (!connected) client_disconnected();
		else
		{
			send_data_type_request_command("GstMessageType", GstDebugger::TypeDescriptionRequest_Type_ENUM_FLAGS);
			send_data_type_request_command("GstEventType", GstDebugger::TypeDescriptionRequest_Type_ENUM_FLAGS);
			send_data_type_request_command("GstQueryType", GstDebugger::TypeDescriptionRequest_Type_ENUM_FLAGS);
			send_data_type_request_command("GstDebugLevel", GstDebugger::TypeDescriptionRequest_Type_ENUM_FLAGS);
			send_data_type_request_command("GstState", GstDebugger::TypeDescriptionRequest_Type_ENUM_FLAGS);
			send_request_entire_topology_command();
			send_request_debug_categories_command();
		}
	});
}

int Controller::run(int &argc, char **&argv)
{
	Glib::RefPtr<Gtk::Application> app = Gtk::Application::create(argc, argv, "eu.cookandcommit.gst-debugger");
	app->set_flags(app->get_flags() | Gio::APPLICATION_NON_UNIQUE);
	view->set_controller(shared_from_this());
	return app->run(*view);
}

void Controller::process_frame(const GstDebugger::GStreamerData &data)
{
	on_frame_received(data);

	switch (data.info_type_case())
	{
	case GstDebugger::GStreamerData::kDebugCategories:
		debug_categories.clear();
		for (auto c : data.debug_categories().category())
			debug_categories.push_back(c);
		on_debug_categories_changed();
		break;
	case GstDebugger::GStreamerData::kEnumFlagsType:
		update_enum_model(data.enum_flags_type());
		on_enum_list_changed(data.enum_flags_type().type_name(), true);
		break;
	case GstDebugger::GStreamerData::kConfirmation:
		on_confirmation_received(data.confirmation());
		break;
	case GstDebugger::GStreamerData::kTopologyInfo:
		process(data.topology_info());
		on_model_changed(current_model);

		if (data.topology_info().has_element())
		{
			if (!get_factory(data.topology_info().element().factory_name()))
				send_data_type_request_command(data.topology_info().element().factory_name(), GstDebugger::TypeDescriptionRequest_Type_FACTORY);
			if (!get_klass(data.topology_info().element().type_name()))
				send_data_type_request_command(data.topology_info().element().type_name(), GstDebugger::TypeDescriptionRequest_Type_KLASS);
		}
		break;
	case GstDebugger::GStreamerData::kFactory:
		update_factory_model(data.factory());
		on_factory_list_changed(data.factory().name(), true);
		break;
	case GstDebugger::GStreamerData::kElementKlass:
		update_klass_model(data.element_klass());
		on_klass_list_changed(data.element_klass().name(), true);
		break;
	case GstDebugger::GStreamerData::kPropertyValue:
		add_property(data.property_value());
		on_property_value_received(data.property_value());
		break;
	case GstDebugger::GStreamerData::kPadDynamicInfo:
		update_pad_dynamic_info(data.pad_dynamic_info());
		break;
	case GstDebugger::GStreamerData::kQueryInfo:
		on_query_received(data.query_info());
		break;
	case GstDebugger::GStreamerData::kEventInfo:
		on_event_received(data.event_info());
		break;
	case GstDebugger::GStreamerData::kMessageInfo:
		on_message_received(data.message_info());
		break;
	case GstDebugger::GStreamerData::kBufferInfo:
		on_buffer_received(data.buffer_info());
		break;
	case GstDebugger::GStreamerData::kLogInfo:
		on_log_received(data.log_info());
		break;
    case GstDebugger::GStreamerData::kServerError:
        on_error_received(data.server_error());
    case GstDebugger::GStreamerData::INFO_TYPE_NOT_SET:
        // TODO: error
        break;
	}
}

template<typename T>
boost::optional<T> get_from_container(const std::vector<T>& container, const std::string &name, std::function<std::string(const T& val)> get_t_name)
{
	auto it = std::find_if(container.begin(), container.end(), [name, get_t_name](const T& enum_type) {
		return get_t_name(enum_type) == name;
	});

	if (it == container.end())
		return boost::none;
	else
		return *it;
}

boost::optional<GstEnumType> Controller::get_enum_type(const std::string &name)
{
	return get_from_container<GstEnumType>(enum_container, name, [](const GstEnumType& enum_type) {return enum_type.get_name(); } );
}

boost::optional<FactoryModel> Controller::get_factory(const std::string &name)
{
	return get_from_container<FactoryModel>(factory_container, name, [](const FactoryModel& factory) {return factory.get_name(); } );
}

boost::optional<KlassModel> Controller::get_klass(const std::string &name)
{
	return get_from_container<KlassModel>(klass_container, name, [](const KlassModel& klass) {return klass.get_name(); } );
}

void Controller::model_up()
{
	if (current_model == ElementModel::get_root())
		return;

	current_model = std::static_pointer_cast<ElementModel>(current_model->get_parent());
	on_model_changed(current_model);
}

void Controller::model_down(const std::string &name)
{
	auto tmp = current_model->get_child(name);

	if (tmp && tmp->is_bin())
	{
		current_model = tmp;
		on_model_changed(current_model);
	}
}

void Controller::set_selected_object(const std::string &name)
{
	auto colon_pos = name.find(':');
	bool is_pad = colon_pos != std::string::npos;
	std::shared_ptr<ObjectModel> obj;

	if (!is_pad)
	{
		obj = current_model->get_child(name);
	}
	else
	{
		std::string e_name = name.substr(0, colon_pos);
		std::string p_name = name.substr(colon_pos+1);
		std::shared_ptr<ElementModel> e_model;
		if ((e_model = std::dynamic_pointer_cast<ElementModel>(current_model->get_child(e_name))))
		{
			obj = e_model->get_pad(p_name);
		}
	}

	if (selected_object != obj)
	{
		selected_object = obj;
		on_selected_object_changed();

		if (obj && is_pad)
		{
			send_request_pad_dynamic_info(ElementPathProcessor::get_object_path(obj));
		}
	}
}

void Controller::update_enum_model(const GstDebugger::EnumFlagsType &enum_type)
{
	GstEnumType et(enum_type.type_name(),
			enum_type.kind() == GstDebugger::EnumFlagsType_EnumFlagsKind_ENUM ? G_TYPE_ENUM : G_TYPE_FLAGS);
	for (int i = 0; i < enum_type.values_size(); i++)
	{
		et.add_value(enum_type.values(i).name(), enum_type.values(i).value(), enum_type.values(i).nick());
	}

	// todo copy & paste get_enum_type()
	auto it = std::find_if(enum_container.begin(), enum_container.end(), [enum_type](const GstEnumType& type) {
		return type.get_name() == enum_type.type_name();
	});

	if (it == enum_container.end())
	{
		enum_container.push_back(et);
	}
	else
	{
		*it = et;
	}
}

void Controller::update_factory_model(const GstDebugger::FactoryType &factory_info)
{
	FactoryModel model(factory_info.name());

	for (int i = 0; i < factory_info.templates_size(); i++)
	{
		model.append_template(protocol_template_to_gst_template(factory_info.templates(i)));
	}

	for (int i = 0; i < factory_info.metadata_size(); i++)
	{
		model.append_meta(factory_info.metadata(i).key(), factory_info.metadata(i).value());
	}

	// todo copy & paste get_enum_type()
	auto it = std::find_if(factory_container.begin(), factory_container.end(), [model](const FactoryModel& type) {
		return type.get_name() == model.get_name();
	});

	if (it == factory_container.end())
	{
		factory_container.push_back(model);
	}
	else
	{
		*it = model;
	}
}

void Controller::update_klass_model(const GstDebugger::ElementKlass &klass_info)
{
	KlassModel model(klass_info.name());

	// todo copy & paste get_enum_type()
	auto it = std::find_if(klass_container.begin(), klass_container.end(), [model](const KlassModel& type) {
		return type.get_name() == model.get_name();
	});

	for (auto property : klass_info.property_info())
	{
		GValue *g_val = new GValue;
		*g_val = {0};
		g_value_deserialize(g_val, property.default_value().gtype(), (InternalGType)property.default_value().internal_type(),
				property.default_value().data().c_str(), property.default_value().data().length());
		model.append_property(PropertyModel(property.name(), property.nick(),
				property.blurb(), (GParamFlags)property.flags(), std::shared_ptr<GValueBase>(GValueBase::build_gvalue(g_val))));
	}

	if (it == klass_container.end())
	{
		klass_container.push_back(model);
	}
	else
	{
		*it = model;
	}
}

void Controller::add_property(const GstDebugger::PropertyValue &value)
{
	auto element = std::dynamic_pointer_cast<ElementModel>(ElementPathProcessor(value.object()).get_last_obj());

	if (!element)
		return;

	GValue *g_val = new GValue;
	*g_val = {0};
	g_value_deserialize(g_val, value.value().gtype(), (InternalGType)value.value().internal_type(),
			value.value().data().c_str(), value.value().data().length());

	bool had_property = element->has_property(value.name());

	auto vb = element->add_property(value.name(), g_val);

	if (!had_property)
	{
		auto obj = value.object(); auto name = value.name();
		vb->widget_value_changed.connect([this, name, obj, vb]{
			this->send_set_property_command(obj, name, vb->get_gvalue());
		});
	}

	if (G_TYPE_IS_ENUM(g_val->g_type) || G_TYPE_IS_FLAGS(g_val->g_type))
	{
		auto enum_type = get_enum_type(value.value().type_name());
		if (enum_type)
		{
			if (G_TYPE_IS_ENUM(g_val->g_type))
				std::dynamic_pointer_cast<GValueEnum>(vb)->set_type(enum_type.get());
			else
				std::dynamic_pointer_cast<GValueFlags>(vb)->set_type(enum_type.get());
		}
		else
			send_data_type_request_command(value.value().type_name(), GstDebugger::TypeDescriptionRequest_Type_ENUM_FLAGS);
	}
}

std::string Controller::get_selected_pad_path() const
{
	return (selected_object && std::dynamic_pointer_cast<PadModel>(selected_object)) ?
			ElementPathProcessor::get_object_path(selected_object) :
			std::string();
}

void Controller::log(const std::string &message)
{
	// todo date/time?
	std::cerr << message << std::endl;
}

void Controller::client_disconnected()
{
	auto root_model = ElementModel::get_root();
	root_model->clean_model();
	on_model_changed(root_model);

	selected_object = std::shared_ptr<ObjectModel>();
	on_selected_object_changed();

	while (enum_container.begin() != enum_container.end())
	{
		std::string name = enum_container.begin()->get_name();
		enum_container.erase(enum_container.begin());
		on_enum_list_changed(name, false);
	}
	while (factory_container.begin() != factory_container.end())
	{
		std::string name = factory_container.begin()->get_name();
		factory_container.erase(factory_container.begin());
		on_factory_list_changed(name, false);
	}
	while (klass_container.begin() != klass_container.end())
	{
		std::string name = klass_container.begin()->get_name();
		klass_container.erase(klass_container.begin());
		on_klass_list_changed(name, false);
	}

	debug_categories.clear();
	on_debug_categories_changed();
}

void Controller::update_pad_dynamic_info(const GstDebugger::PadDynamicInfo& info)
{
	auto pad = std::dynamic_pointer_cast<PadModel>(ElementPathProcessor(info.pad()).get_last_obj());

	if (!pad)
		return;

	pad->set_current_caps(Gst::Caps::create_from_string(info.current_caps()));
	pad->set_allowed_caps(Gst::Caps::create_from_string(info.allowed_caps()));

	if (selected_object == pad)
	{
		on_selected_object_changed();
	}
}

