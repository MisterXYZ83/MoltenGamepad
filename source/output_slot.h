#ifndef OUTPUT_SLOT_H
#define OUTPUT_SLOT_H

#include "uinput.h"
#include "eventlists/eventlist.h"
#include <iostream>
#include <string>
#include <map>
#include <memory>

#define OPTION_ACCEPTED 0

class input_source;

class output_slot {
public:
  std::string name;
  std::string descr;
  output_slot(std::string name) : name(name) {};
  output_slot(std::string name, std::string descr) : name(name), descr(descr) {};
  virtual ~output_slot();
  virtual void take_event(struct input_event in) {
  }

  virtual bool accept_device(std::shared_ptr<input_source> dev);
  virtual bool add_device(std::shared_ptr<input_source> dev);
  virtual bool remove_device(input_source* dev);

  void update_option(std::string option, std::string value) {
    if (options.find(option) == options.end()) return;
    if (process_option(option, value) == OPTION_ACCEPTED)
      options[option] = value;
  }

  int pad_count = 0;
  std::map<std::string, std::string> options;
protected:
  int uinput_fd = -1;
  std::vector<std::weak_ptr<input_source>> devices;
  std::mutex lock;

  virtual int process_option(std::string name, std::string value) {
    return -1;
  };
};



struct virtpad_settings {
  uinput_ids u_ids;
  bool dpad_as_hat;
  bool analog_triggers;
  std::string facemap_1234;
};

class virtual_gamepad : public output_slot {
public:
  bool dpad_as_hat = false;
  bool analog_triggers = false;
  virtual_gamepad(std::string name, std::string descr, virtpad_settings settings, uinput* ui);
  virtual void take_event(struct input_event in);
  virtual bool accept_device(std::shared_ptr<input_source> dev);
protected:
  virtual int process_option(std::string name, std::string value);

  virtpad_settings padstyle;

  int face_1234[4] = {BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH};
  void set_face_map(std::string map);
  std::string get_face_map();
  enum acceptancetype { NONE, SINGULAR, GREEDY} acceptance = SINGULAR;
};

class virtual_keyboard : public output_slot {
public:
  virtual_keyboard(std::string name, std::string descr, uinput_ids u_ids, uinput* ui);
  virtual void take_event(struct input_event in) {
    write(uinput_fd, &in, sizeof(in));
  };
protected:

  uinput_ids u_ids;
  virtual int process_option(std::string name, std::string value);
};

class debug_device : public output_slot {
public:
  debug_device(std::string name, std::string descr) : output_slot(name, descr) {};
  virtual void take_event(struct input_event in) {
    if (in.type == EV_KEY) std::cout << name << ": " << in.code << " " << in.value << "(" << get_key_name(in.code) << ")" << std::endl;
    if (in.type == EV_ABS) std::cout << name << ": " << in.code << " " << in.value << "(" << get_axis_name(in.code) << ")" << std::endl;
    if (in.type == EV_REL) std::cout << name << ": " << in.code << " " << in.value << "(" << get_rel_name(in.code) << ")" << std::endl;
  };
};

#endif
