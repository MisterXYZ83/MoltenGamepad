#ifndef SLOT_MANAGER_H
#define SLOT_MANAGER_H

#include <mutex>
#include <vector>

#include "uinput.h"
#include "virtual_devices/virtual_device.h"
#include "virtual_devices/virtual_gamepad.h"
#include "devices/device.h"
#include "protocols/message_stream.h"
#include "options.h"

class input_source;

enum slot_state { SLOT_ACTIVE, SLOT_INACTIVE, SLOT_CLOSED, SLOT_DISABLED};

struct output_slot {
  virtual_device* virt_dev;
  slot_state state;
  bool has_devices;
};


class slot_manager {
public:
  enum id_type {NAME_ID, UNIQ_ID, PHYS_ID};

  slot_manager(int max_pads, bool keys, const virtpad_settings& padstyle);

  ~slot_manager();

  int request_slot(input_source* dev);
  void move_to_slot(input_source* dev, virtual_device* target);
  void id_based_assign(slot_manager::id_type, std::string id, virtual_device* slot); //tie an id to a specific slot for autoassignment
  void for_all_assignments(std::function<void (slot_manager::id_type, std::string, virtual_device*)> func);

  const uinput* get_uinput() { return ui; };

  virtual_device* find_slot(std::string name);
  output_slot keyboard;
  output_slot dummyslot;
  output_slot debugslot;

  std::vector<output_slot> slots;
  std::vector<bool> slot_emptiness_cache;
  message_stream log;
  options opts;

  bool press_start_on_any_disconnect = false;
  bool press_start_on_last_disconnect = false;
  uint start_press_milliseconds;
  void update_slot_emptiness();
  void process_slot_emptiness();
  void tick_all_slots();
private:
  void remove_from(virtual_device* slot);
  void move_device(input_source* dev, virtual_device* target);
  int process_option(std::string& name, MGField value);
  virtual_device* find_id_based_assignment(input_source* dev);

  bool slots_on_demand = false;

  uinput* ui;
  std::mutex lock;
  int min_pads = 1;
  int max_pads;
  int active_pads = 4;
  bool persistent_slots = true;
  std::map<std::pair<id_type,std::string>,virtual_device*> id_slot_assignments;
};

#endif
