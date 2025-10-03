/*----------------------------------------------------------------------------
 ChucK CLAP Chugin

 Allows loading and using CLAP (CLever Audio Plugin) plugins in ChucK.

 Copyright (c) 2025 CCRMA, Stanford University.  All rights reserved.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 U.S.A.
 -----------------------------------------------------------------------------*/

#include "chugin.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include <map>
#include <string>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>

// CLAP headers
#include "clap-headers/include/clap/clap.h"

// Forward declarations
CK_DLL_CTOR(clap_ctor);
CK_DLL_DTOR(clap_dtor);
CK_DLL_TICK(clap_tick);

CK_DLL_MFUN(clap_load);
CK_DLL_MFUN(clap_close);
CK_DLL_MFUN(clap_list);
CK_DLL_MFUN(clap_set_param);
CK_DLL_MFUN(clap_set_param_by_name);
CK_DLL_MFUN(clap_get_param);
CK_DLL_MFUN(clap_get_param_by_name);
CK_DLL_MFUN(clap_get_param_name);
CK_DLL_MFUN(clap_get_param_count);
CK_DLL_MFUN(clap_bypass);
CK_DLL_MFUN(clap_send_midi);
CK_DLL_MFUN(clap_note_on);
CK_DLL_MFUN(clap_note_off);
CK_DLL_MFUN(clap_is_instrument);
CK_DLL_MFUN(clap_get_preset_count);
CK_DLL_MFUN(clap_get_preset_name);
CK_DLL_MFUN(clap_load_preset);
CK_DLL_MFUN(clap_load_preset_by_name);

t_CKINT clap_data_offset = 0;

class CLAPWrapper
{
public:
    CLAPWrapper(t_CKFLOAT sampleRate)
        : m_sampleRate(sampleRate)
        , m_plugin(nullptr)
        , m_library(nullptr)
        , m_entry(nullptr)
        , m_bypass(false)
        , m_isInstrument(false)
        , m_activated(false)
        , m_processing(false)
        , m_params(nullptr)
        , m_audioPortsExt(nullptr)
        , m_notePortsExt(nullptr)
        , m_presetLoad(nullptr)
        , m_presetDiscoveryFactory(nullptr)
        , m_presetDiscoveryProvider(nullptr)
    {
        // Initialize host
        m_host.clap_version = CLAP_VERSION;
        m_host.host_data = this;
        m_host.name = "ChucK";
        m_host.vendor = "CCRMA";
        m_host.url = "https://chuck.cs.princeton.edu";
        m_host.version = "1.5.5.0";
        m_host.get_extension = host_get_extension;
        m_host.request_restart = host_request_restart;
        m_host.request_process = host_request_process;
        m_host.request_callback = host_request_callback;

        // Initialize process data
        memset(&m_process, 0, sizeof(m_process));
        m_process.steady_time = 0;
        m_process.frames_count = 1;

        // Allocate audio buffers
        m_inputData[0] = 0.0f;
        m_outputData[0] = 0.0f;
        m_outputData[1] = 0.0f;

        // Setup pointers for data32 (array of channel pointers)
        m_inputChannelPtr[0] = &m_inputData[0];
        m_outputChannelPtr[0] = &m_outputData[0];
        m_outputChannelPtr[1] = &m_outputData[1];

        // Setup audio buffers
        m_inputBuffer.data32 = m_inputChannelPtr;
        m_inputBuffer.data64 = nullptr;
        m_inputBuffer.channel_count = 1;
        m_inputBuffer.latency = 0;
        m_inputBuffer.constant_mask = 0;

        m_outputBuffer.data32 = m_outputChannelPtr;
        m_outputBuffer.data64 = nullptr;
        m_outputBuffer.channel_count = 2;
        m_outputBuffer.latency = 0;
        m_outputBuffer.constant_mask = 0;

        m_process.audio_inputs = &m_inputBuffer;
        m_process.audio_inputs_count = 1;
        m_process.audio_outputs = &m_outputBuffer;
        m_process.audio_outputs_count = 1;

        // Initialize event buffers
        m_inputEvents.ctx = this;
        m_inputEvents.size = event_list_size;
        m_inputEvents.get = event_list_get;

        m_outputEvents.ctx = this;
        m_outputEvents.try_push = event_list_try_push;

        m_process.in_events = &m_inputEvents;
        m_process.out_events = &m_outputEvents;
    }

    ~CLAPWrapper()
    {
        close();
        cleanupPresetDiscovery();
    }

    bool load(const char* path)
    {
        close();

        // Load dynamic library
        m_library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if(!m_library)
        {
            fprintf(stderr, "[CLAP]: Could not load library: %s\n", dlerror());
            return false;
        }

        // Get entry point
        m_entry = (const clap_plugin_entry_t*)dlsym(m_library, "clap_entry");
        if(!m_entry)
        {
            fprintf(stderr, "[CLAP]: Could not find clap_entry symbol\n");
            dlclose(m_library);
            m_library = nullptr;
            return false;
        }

        // Initialize entry
        if(!m_entry->init(path))
        {
            fprintf(stderr, "[CLAP]: Failed to initialize plugin entry\n");
            dlclose(m_library);
            m_library = nullptr;
            m_entry = nullptr;
            return false;
        }

        // Get plugin factory
        const clap_plugin_factory_t* factory =
            (const clap_plugin_factory_t*)m_entry->get_factory(CLAP_PLUGIN_FACTORY_ID);

        if(!factory)
        {
            fprintf(stderr, "[CLAP]: Could not get plugin factory\n");
            m_entry->deinit();
            dlclose(m_library);
            m_library = nullptr;
            m_entry = nullptr;
            return false;
        }

        // Get first plugin (or could search by ID)
        uint32_t plugin_count = factory->get_plugin_count(factory);
        if(plugin_count == 0)
        {
            fprintf(stderr, "[CLAP]: No plugins found in library\n");
            m_entry->deinit();
            dlclose(m_library);
            m_library = nullptr;
            m_entry = nullptr;
            return false;
        }

        const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
        if(!desc)
        {
            fprintf(stderr, "[CLAP]: Could not get plugin descriptor\n");
            m_entry->deinit();
            dlclose(m_library);
            m_library = nullptr;
            m_entry = nullptr;
            return false;
        }

        // Check if it's an instrument
        if(desc->features)
        {
            for(int i = 0; desc->features[i]; i++)
            {
                if(strcmp(desc->features[i], CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0 ||
                   strcmp(desc->features[i], CLAP_PLUGIN_FEATURE_SYNTHESIZER) == 0)
                {
                    m_isInstrument = true;
                    break;
                }
            }
        }

        // Create plugin instance
        m_plugin = factory->create_plugin(factory, &m_host, desc->id);
        if(!m_plugin)
        {
            fprintf(stderr, "[CLAP]: Could not create plugin instance\n");
            m_entry->deinit();
            dlclose(m_library);
            m_library = nullptr;
            m_entry = nullptr;
            return false;
        }

        // Initialize plugin
        if(!m_plugin->init(m_plugin))
        {
            fprintf(stderr, "[CLAP]: Could not initialize plugin\n");
            m_plugin->destroy(m_plugin);
            m_plugin = nullptr;
            m_entry->deinit();
            dlclose(m_library);
            m_library = nullptr;
            m_entry = nullptr;
            return false;
        }

        // Get extensions
        m_params = (const clap_plugin_params_t*)m_plugin->get_extension(m_plugin, CLAP_EXT_PARAMS);
        m_audioPortsExt = (const clap_plugin_audio_ports_t*)m_plugin->get_extension(m_plugin, CLAP_EXT_AUDIO_PORTS);
        m_notePortsExt = (const clap_plugin_note_ports_t*)m_plugin->get_extension(m_plugin, CLAP_EXT_NOTE_PORTS);
        m_presetLoad = (const clap_plugin_preset_load_t*)m_plugin->get_extension(m_plugin, CLAP_EXT_PRESET_LOAD);

        // Cache parameters
        cacheParameters();

        // Initialize preset discovery
        initPresetDiscovery(path);

        // Activate plugin
        if(!m_plugin->activate(m_plugin, m_sampleRate, 1, 8192))
        {
            fprintf(stderr, "[CLAP]: Could not activate plugin\n");
            m_plugin->destroy(m_plugin);
            m_plugin = nullptr;
            m_entry->deinit();
            dlclose(m_library);
            m_library = nullptr;
            m_entry = nullptr;
            return false;
        }
        m_activated = true;

        // Start processing
        if(!m_plugin->start_processing(m_plugin))
        {
            fprintf(stderr, "[CLAP]: Could not start processing\n");
            m_plugin->deactivate(m_plugin);
            m_activated = false;
            m_plugin->destroy(m_plugin);
            m_plugin = nullptr;
            m_entry->deinit();
            dlclose(m_library);
            m_library = nullptr;
            m_entry = nullptr;
            return false;
        }
        m_processing = true;

        return true;
    }

    void close()
    {
        if(m_plugin)
        {
            if(m_processing)
            {
                m_plugin->stop_processing(m_plugin);
                m_processing = false;
            }

            if(m_activated)
            {
                m_plugin->deactivate(m_plugin);
                m_activated = false;
            }

            m_plugin->destroy(m_plugin);
            m_plugin = nullptr;
        }

        if(m_entry)
        {
            m_entry->deinit();
            m_entry = nullptr;
        }

        if(m_library)
        {
            dlclose(m_library);
            m_library = nullptr;
        }

        m_parameters.clear();
        m_isInstrument = false;
        m_params = nullptr;
        m_audioPortsExt = nullptr;
        m_notePortsExt = nullptr;
        m_presetLoad = nullptr;
        cleanupPresetDiscovery();
    }

    SAMPLE tick(SAMPLE input)
    {
        if(!m_plugin || !m_processing || m_bypass)
            return input;

        // Set input
        *m_inputChannelPtr[0] = input;

        // Clear output
        *m_outputChannelPtr[0] = 0.0f;
        *m_outputChannelPtr[1] = 0.0f;

        // Process
        clap_process_status status = m_plugin->process(m_plugin, &m_process);

        // Clear event list for next process
        m_eventQueue.clear();

        // Update steady time
        m_process.steady_time++;

        if(status == CLAP_PROCESS_ERROR)
        {
            static bool errorLogged = false;
            if(!errorLogged)
            {
                fprintf(stderr, "[CLAP]: Processing error\n");
                errorLogged = true;
            }
            return m_isInstrument ? 0.0 : input;
        }

        // Return mono output (left channel)
        return *m_outputChannelPtr[0];
    }

    bool setParameter(t_CKINT index, t_CKFLOAT value)
    {
        if(!m_params || index < 0 || index >= m_parameters.size())
            return false;

        clap_id param_id = m_parameters[index].id;

        // Create parameter change event
        clap_event_param_value_t event;
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = 0;
        event.param_id = param_id;
        event.cookie = nullptr;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;

        m_eventQueue.push_back(event);
        return true;
    }

    bool setParameterByName(const char* name, t_CKFLOAT value)
    {
        if(!m_params)
            return false;

        for(size_t i = 0; i < m_parameters.size(); i++)
        {
            if(m_parameters[i].name == name)
            {
                return setParameter(i, value);
            }
        }
        return false;
    }

    t_CKFLOAT getParameter(t_CKINT index)
    {
        if(!m_params || index < 0 || index >= m_parameters.size())
            return 0.0;

        clap_id param_id = m_parameters[index].id;
        double value = 0.0;

        if(m_params->get_value(m_plugin, param_id, &value))
            return value;

        return 0.0;
    }

    t_CKFLOAT getParameterByName(const char* name, bool* found = nullptr)
    {
        if(!m_params)
        {
            if(found) *found = false;
            return 0.0;
        }

        for(size_t i = 0; i < m_parameters.size(); i++)
        {
            if(m_parameters[i].name == name)
            {
                if(found) *found = true;
                return getParameter(i);
            }
        }
        if(found) *found = false;
        return 0.0;
    }

    const char* getParameterName(t_CKINT index)
    {
        if(index < 0 || index >= m_parameters.size())
            return "";
        return m_parameters[index].name.c_str();
    }

    t_CKINT getParameterCount()
    {
        return m_parameters.size();
    }

    void setBypass(bool bypass)
    {
        m_bypass = bypass;
    }

    // MIDI methods
    bool sendMIDI(t_CKINT status, t_CKINT data1, t_CKINT data2)
    {
        if(!m_plugin || !m_processing || !m_isInstrument)
            return false;

        uint8_t channel = status & 0x0F;
        uint8_t messageType = status & 0xF0;

        clap_event_note_t noteEvent;
        memset(&noteEvent, 0, sizeof(noteEvent));
        noteEvent.header.size = sizeof(noteEvent);
        noteEvent.header.time = 0;
        noteEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        noteEvent.header.flags = 0;
        noteEvent.note_id = -1;
        noteEvent.port_index = 0;
        noteEvent.channel = channel;
        noteEvent.key = data1;
        noteEvent.velocity = data2 / 127.0;

        switch(messageType)
        {
            case 0x90: // Note On
                noteEvent.header.type = CLAP_EVENT_NOTE_ON;
                m_eventQueue.push_back(*(clap_event_param_value_t*)&noteEvent);
                break;

            case 0x80: // Note Off
                noteEvent.header.type = CLAP_EVENT_NOTE_OFF;
                m_eventQueue.push_back(*(clap_event_param_value_t*)&noteEvent);
                break;

            case 0xB0: // Control Change
            case 0xC0: // Program Change
                // CLAP uses parameter changes for CC
                return true;

            default:
                return false;
        }

        return true;
    }

    bool noteOn(t_CKINT pitch, t_CKINT velocity)
    {
        return sendMIDI(0x90, pitch, velocity);
    }

    bool noteOff(t_CKINT pitch)
    {
        return sendMIDI(0x80, pitch, 0);
    }

    bool isInstrument() const
    {
        return m_isInstrument;
    }

    // Preset methods
    t_CKINT getPresetCount() const
    {
        return m_presets.size();
    }

    const char* getPresetName(t_CKINT index) const
    {
        if(index < 0 || index >= m_presets.size())
            return "";
        return m_presets[index].name.c_str();
    }

    bool loadPreset(t_CKINT index)
    {
        if(!m_presetLoad || index < 0 || index >= m_presets.size())
            return false;

        const PresetInfo& preset = m_presets[index];
        return m_presetLoad->from_location(m_plugin, preset.location_kind,
                                           preset.location.c_str(),
                                           preset.load_key.empty() ? nullptr : preset.load_key.c_str());
    }

    bool loadPresetByName(const char* name)
    {
        if(!m_presetLoad)
            return false;

        for(size_t i = 0; i < m_presets.size(); i++)
        {
            if(m_presets[i].name == name)
            {
                return loadPreset(i);
            }
        }
        return false;
    }

    static void listCLAPPlugins()
    {
        std::vector<std::string> searchPaths;

#ifdef __APPLE__
        searchPaths.push_back("/Library/Audio/Plug-Ins/CLAP");
        searchPaths.push_back(getenv("HOME") + std::string("/Library/Audio/Plug-Ins/CLAP"));
#elif defined(_WIN32)
        searchPaths.push_back(getenv("COMMONPROGRAMFILES") + std::string("\\CLAP"));
        searchPaths.push_back(getenv("LOCALAPPDATA") + std::string("\\Programs\\Common\\CLAP"));
#else // Linux
        searchPaths.push_back("/usr/lib/clap");
        searchPaths.push_back("/usr/local/lib/clap");
        searchPaths.push_back(getenv("HOME") + std::string("/.clap"));
#endif

        // Check CLAP_PATH environment variable
        const char* clapPath = getenv("CLAP_PATH");
        if(clapPath)
        {
            searchPaths.push_back(clapPath);
        }

        fprintf(stderr, "\n[CLAP]: Available CLAP Plugins:\n");
        fprintf(stderr, "----------------------------------------\n");

        int count = 0;
        for(const auto& path : searchPaths)
        {
            DIR* dir = opendir(path.c_str());
            if(!dir) continue;

            struct dirent* entry;
            while((entry = readdir(dir)) != nullptr)
            {
                if(entry->d_type == DT_REG || entry->d_type == DT_LNK)
                {
                    std::string filename = entry->d_name;
                    if(filename.length() > 5 && filename.substr(filename.length() - 5) == ".clap")
                    {
                        fprintf(stderr, "%3d. %s\n", ++count, filename.c_str());
                        fprintf(stderr, "     Path: %s/%s\n", path.c_str(), filename.c_str());
                    }
                }
            }
            closedir(dir);
        }

        fprintf(stderr, "----------------------------------------\n");
        fprintf(stderr, "Total: %d CLAP plugins\n\n", count);
    }

private:
    struct ParameterInfo
    {
        clap_id id;
        std::string name;
    };

    struct PresetInfo
    {
        std::string name;
        std::string location;
        std::string load_key;
        uint32_t location_kind;
    };

    void cacheParameters()
    {
        m_parameters.clear();

        if(!m_params)
            return;

        uint32_t count = m_params->count(m_plugin);
        for(uint32_t i = 0; i < count; i++)
        {
            clap_param_info_t info;
            if(m_params->get_info(m_plugin, i, &info))
            {
                ParameterInfo param;
                param.id = info.id;
                param.name = info.name;
                m_parameters.push_back(param);
            }
        }
    }

    // Host callbacks
    static const void* CLAP_ABI host_get_extension(const clap_host_t* host, const char* extension_id)
    {
        // We don't provide any host extensions for now
        return nullptr;
    }

    static void CLAP_ABI host_request_restart(const clap_host_t* host)
    {
        // Not implemented - would require stopping and restarting plugin
    }

    static void CLAP_ABI host_request_process(const clap_host_t* host)
    {
        // Not implemented - we're always processing
    }

    static void CLAP_ABI host_request_callback(const clap_host_t* host)
    {
        // Not implemented - would require main thread callback
    }

    // Preset discovery support
    void initPresetDiscovery(const char* path)
    {
        if(!m_entry)
            return;

        // Get preset discovery factory
        m_presetDiscoveryFactory = (const clap_preset_discovery_factory_t*)
            m_entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID);

        if(!m_presetDiscoveryFactory)
            return;

        // Create indexer
        clap_preset_discovery_indexer_t indexer;
        memset(&indexer, 0, sizeof(indexer));
        indexer.clap_version = CLAP_VERSION;
        indexer.name = "ChucK";
        indexer.vendor = "CCRMA";
        indexer.url = "https://chuck.cs.princeton.edu";
        indexer.version = "1.5.5.0";
        indexer.indexer_data = this;
        indexer.declare_filetype = indexer_declare_filetype;
        indexer.declare_location = indexer_declare_location;
        indexer.declare_soundpack = indexer_declare_soundpack;
        indexer.get_extension = nullptr;

        // Get plugin descriptor to find provider
        const clap_plugin_factory_t* factory =
            (const clap_plugin_factory_t*)m_entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
        if(!factory)
            return;

        const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
        if(!desc || !desc->id)
            return;

        // Try to create provider for this plugin
        uint32_t provider_count = m_presetDiscoveryFactory->count(m_presetDiscoveryFactory);
        for(uint32_t i = 0; i < provider_count; i++)
        {
            const clap_preset_discovery_provider_descriptor_t* provider_desc =
                m_presetDiscoveryFactory->get_descriptor(m_presetDiscoveryFactory, i);
            if(!provider_desc)
                continue;

            m_presetDiscoveryProvider = m_presetDiscoveryFactory->create(
                m_presetDiscoveryFactory, &indexer, provider_desc->id);

            if(m_presetDiscoveryProvider)
            {
                if(m_presetDiscoveryProvider->init(m_presetDiscoveryProvider))
                {
                    // Discovery successful - locations and presets will be collected via callbacks
                    discoverPresets();
                    break;
                }
                else
                {
                    m_presetDiscoveryProvider->destroy(m_presetDiscoveryProvider);
                    m_presetDiscoveryProvider = nullptr;
                }
            }
        }
    }

    void cleanupPresetDiscovery()
    {
        if(m_presetDiscoveryProvider)
        {
            m_presetDiscoveryProvider->destroy(m_presetDiscoveryProvider);
            m_presetDiscoveryProvider = nullptr;
        }
        m_presetDiscoveryFactory = nullptr;
        m_presets.clear();
        m_discoveryLocations.clear();
    }

    void discoverPresets()
    {
        if(!m_presetDiscoveryProvider)
            return;

        // Create metadata receiver
        clap_preset_discovery_metadata_receiver_t receiver;
        memset(&receiver, 0, sizeof(receiver));
        receiver.receiver_data = this;
        receiver.on_error = receiver_on_error;
        receiver.begin_preset = receiver_begin_preset;
        receiver.add_plugin_id = receiver_add_plugin_id;
        receiver.set_soundpack_id = receiver_set_soundpack_id;
        receiver.set_flags = receiver_set_flags;
        receiver.add_creator = receiver_add_creator;
        receiver.set_description = receiver_set_description;
        receiver.set_timestamps = receiver_set_timestamps;
        receiver.add_feature = receiver_add_feature;
        receiver.add_extra_info = receiver_add_extra_info;

        // Crawl declared locations for presets
        for(const auto& loc : m_discoveryLocations)
        {
            m_currentPresetLocation = loc;
            m_presetDiscoveryProvider->get_metadata(m_presetDiscoveryProvider,
                                                    loc.kind, loc.location.c_str(), &receiver);
        }
    }

    // Indexer callbacks
    static bool CLAP_ABI indexer_declare_filetype(const clap_preset_discovery_indexer_t* indexer,
                                                   const clap_preset_discovery_filetype_t* filetype)
    {
        // We accept all file types
        return true;
    }

    static bool CLAP_ABI indexer_declare_location(const clap_preset_discovery_indexer_t* indexer,
                                                   const clap_preset_discovery_location_t* location)
    {
        CLAPWrapper* wrapper = (CLAPWrapper*)indexer->indexer_data;
        if(!location)
            return false;

        DiscoveryLocation loc;
        loc.name = location->name ? location->name : "";
        loc.location = location->location ? location->location : "";
        loc.kind = location->kind;
        loc.flags = location->flags;

        wrapper->m_discoveryLocations.push_back(loc);
        return true;
    }

    static bool CLAP_ABI indexer_declare_soundpack(const clap_preset_discovery_indexer_t* indexer,
                                                    const clap_preset_discovery_soundpack_t* soundpack)
    {
        // We don't track soundpacks for now
        return true;
    }

    // Metadata receiver callbacks
    static void CLAP_ABI receiver_on_error(const clap_preset_discovery_metadata_receiver_t* receiver,
                                           int32_t os_error, const char* error_message)
    {
        // Silently ignore errors for now
    }

    static bool CLAP_ABI receiver_begin_preset(const clap_preset_discovery_metadata_receiver_t* receiver,
                                                const char* name, const char* load_key)
    {
        CLAPWrapper* wrapper = (CLAPWrapper*)receiver->receiver_data;

        PresetInfo preset;
        preset.name = name ? name : "";
        preset.load_key = load_key ? load_key : "";
        preset.location = wrapper->m_currentPresetLocation.location;
        preset.location_kind = wrapper->m_currentPresetLocation.kind;

        wrapper->m_presets.push_back(preset);
        return true;
    }

    static void CLAP_ABI receiver_add_plugin_id(const clap_preset_discovery_metadata_receiver_t* receiver,
                                                 const clap_universal_plugin_id_t* plugin_id)
    {
        // Not used
    }

    static void CLAP_ABI receiver_set_soundpack_id(const clap_preset_discovery_metadata_receiver_t* receiver,
                                                    const char* soundpack_id)
    {
        // Not used
    }

    static void CLAP_ABI receiver_set_flags(const clap_preset_discovery_metadata_receiver_t* receiver,
                                            uint32_t flags)
    {
        // Not used
    }

    static void CLAP_ABI receiver_add_creator(const clap_preset_discovery_metadata_receiver_t* receiver,
                                              const char* creator)
    {
        // Not used
    }

    static void CLAP_ABI receiver_set_description(const clap_preset_discovery_metadata_receiver_t* receiver,
                                                   const char* description)
    {
        // Not used
    }

    static void CLAP_ABI receiver_set_timestamps(const clap_preset_discovery_metadata_receiver_t* receiver,
                                                  clap_timestamp creation_time, clap_timestamp modification_time)
    {
        // Not used
    }

    static void CLAP_ABI receiver_add_feature(const clap_preset_discovery_metadata_receiver_t* receiver,
                                              const char* feature)
    {
        // Not used
    }

    static void CLAP_ABI receiver_add_extra_info(const clap_preset_discovery_metadata_receiver_t* receiver,
                                                  const char* key, const char* value)
    {
        // Not used
    }

    // Event list callbacks
    static uint32_t CLAP_ABI event_list_size(const clap_input_events_t* list)
    {
        CLAPWrapper* wrapper = (CLAPWrapper*)list->ctx;
        return wrapper->m_eventQueue.size();
    }

    static const clap_event_header_t* CLAP_ABI event_list_get(
        const clap_input_events_t* list, uint32_t index)
    {
        CLAPWrapper* wrapper = (CLAPWrapper*)list->ctx;
        if(index >= wrapper->m_eventQueue.size())
            return nullptr;
        return (const clap_event_header_t*)&wrapper->m_eventQueue[index];
    }

    static bool CLAP_ABI event_list_try_push(const clap_output_events_t* list,
                                              const clap_event_header_t* event)
    {
        // We don't handle output events for now
        return true;
    }

    clap_host_t m_host;
    const clap_plugin_t* m_plugin;
    void* m_library;
    const clap_plugin_entry_t* m_entry;

    t_CKFLOAT m_sampleRate;
    bool m_bypass;
    bool m_isInstrument;
    bool m_activated;
    bool m_processing;

    // Extensions
    const clap_plugin_params_t* m_params;
    const clap_plugin_audio_ports_t* m_audioPortsExt;
    const clap_plugin_note_ports_t* m_notePortsExt;
    const clap_plugin_preset_load_t* m_presetLoad;
    const clap_preset_discovery_factory_t* m_presetDiscoveryFactory;
    const clap_preset_discovery_provider_t* m_presetDiscoveryProvider;

    // Audio buffers
    float m_inputData[1];
    float m_outputData[2];
    float* m_inputChannelPtr[1];
    float* m_outputChannelPtr[2];
    clap_audio_buffer_t m_inputBuffer;
    clap_audio_buffer_t m_outputBuffer;

    // Process data
    clap_process_t m_process;
    clap_input_events_t m_inputEvents;
    clap_output_events_t m_outputEvents;

    // Event queue
    std::vector<clap_event_param_value_t> m_eventQueue;

    std::vector<ParameterInfo> m_parameters;

    // Preset discovery
    struct DiscoveryLocation
    {
        std::string name;
        std::string location;
        uint32_t kind;
        uint32_t flags;
    };

    std::vector<PresetInfo> m_presets;
    std::vector<DiscoveryLocation> m_discoveryLocations;
    DiscoveryLocation m_currentPresetLocation;
};

// ChucK DLL Query
CK_DLL_QUERY(CLAP)
{
    QUERY->setname(QUERY, "CLAP");

    QUERY->begin_class(QUERY, "CLAP", "UGen");
    QUERY->doc_class(QUERY, "Load and use CLAP (CLever Audio Plugin) plugins in ChucK. "
                            "CLAP plugins can be effects or instruments. "
                            "CLAP is an open-source plugin standard.");
    QUERY->add_ex(QUERY, "effects/CLAP.ck");

    QUERY->add_ctor(QUERY, clap_ctor);
    QUERY->add_dtor(QUERY, clap_dtor);

    QUERY->add_ugen_func(QUERY, clap_tick, NULL, 1, 1);

    QUERY->add_mfun(QUERY, clap_load, "int", "load");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->doc_func(QUERY, "Load a CLAP plugin by file path. "
                          "Returns 1 on success, 0 on failure.");

    QUERY->add_mfun(QUERY, clap_close, "void", "close");
    QUERY->doc_func(QUERY, "Close the currently loaded CLAP plugin.");

    QUERY->add_mfun(QUERY, clap_list, "void", "list");
    QUERY->doc_func(QUERY, "List all available CLAP plugins on the system.");

    QUERY->add_mfun(QUERY, clap_set_param, "void", "setParam");
    QUERY->add_arg(QUERY, "int", "index");
    QUERY->add_arg(QUERY, "float", "value");
    QUERY->doc_func(QUERY, "Set a parameter value by index.");

    QUERY->add_mfun(QUERY, clap_set_param_by_name, "int", "setParamByName");
    QUERY->add_arg(QUERY, "string", "name");
    QUERY->add_arg(QUERY, "float", "value");
    QUERY->doc_func(QUERY, "Set a parameter value by name. Returns 1 on success, 0 if parameter not found.");

    QUERY->add_mfun(QUERY, clap_get_param, "float", "getParam");
    QUERY->add_arg(QUERY, "int", "index");
    QUERY->doc_func(QUERY, "Get a parameter value by index.");

    QUERY->add_mfun(QUERY, clap_get_param_by_name, "float", "getParamByName");
    QUERY->add_arg(QUERY, "string", "name");
    QUERY->doc_func(QUERY, "Get a parameter value by name. Returns 0.0 if parameter not found.");

    QUERY->add_mfun(QUERY, clap_get_param_name, "string", "paramName");
    QUERY->add_arg(QUERY, "int", "index");
    QUERY->doc_func(QUERY, "Get a parameter name by index.");

    QUERY->add_mfun(QUERY, clap_get_param_count, "int", "paramCount");
    QUERY->doc_func(QUERY, "Get the number of parameters available.");

    QUERY->add_mfun(QUERY, clap_bypass, "void", "bypass");
    QUERY->add_arg(QUERY, "int", "bypass");
    QUERY->doc_func(QUERY, "Bypass the CLAP plugin (1 = bypass, 0 = active).");

    // MIDI methods
    QUERY->add_mfun(QUERY, clap_send_midi, "int", "sendMIDI");
    QUERY->add_arg(QUERY, "int", "status");
    QUERY->add_arg(QUERY, "int", "data1");
    QUERY->add_arg(QUERY, "int", "data2");
    QUERY->doc_func(QUERY, "Send raw MIDI message to CLAP plugin (for instrument types). Returns 1 on success.");

    QUERY->add_mfun(QUERY, clap_note_on, "int", "noteOn");
    QUERY->add_arg(QUERY, "int", "pitch");
    QUERY->add_arg(QUERY, "int", "velocity");
    QUERY->doc_func(QUERY, "Send MIDI note-on message (channel 0). Returns 1 on success.");

    QUERY->add_mfun(QUERY, clap_note_off, "int", "noteOff");
    QUERY->add_arg(QUERY, "int", "pitch");
    QUERY->doc_func(QUERY, "Send MIDI note-off message (channel 0). Returns 1 on success.");

    QUERY->add_mfun(QUERY, clap_is_instrument, "int", "isInstrument");
    QUERY->doc_func(QUERY, "Check if loaded CLAP plugin is an instrument. Returns 1 if true.");

    // Preset methods
    QUERY->add_mfun(QUERY, clap_get_preset_count, "int", "presetCount");
    QUERY->doc_func(QUERY, "Get the number of available presets.");

    QUERY->add_mfun(QUERY, clap_get_preset_name, "string", "presetName");
    QUERY->add_arg(QUERY, "int", "index");
    QUERY->doc_func(QUERY, "Get the name of a preset by index.");

    QUERY->add_mfun(QUERY, clap_load_preset, "int", "loadPreset");
    QUERY->add_arg(QUERY, "int", "index");
    QUERY->doc_func(QUERY, "Load a preset by index. Returns 1 on success.");

    QUERY->add_mfun(QUERY, clap_load_preset_by_name, "int", "loadPresetByName");
    QUERY->add_arg(QUERY, "string", "name");
    QUERY->doc_func(QUERY, "Load a preset by name. Returns 1 on success.");

    clap_data_offset = QUERY->add_mvar(QUERY, "int", "@clap_data", false);

    QUERY->end_class(QUERY);

    return TRUE;
}

// Implementation
CK_DLL_CTOR(clap_ctor)
{
    OBJ_MEMBER_INT(SELF, clap_data_offset) = 0;

    CLAPWrapper* wrapper = new CLAPWrapper(API->vm->srate(VM));

    OBJ_MEMBER_INT(SELF, clap_data_offset) = (t_CKINT)wrapper;
}

CK_DLL_DTOR(clap_dtor)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    if(wrapper)
    {
        delete wrapper;
        OBJ_MEMBER_INT(SELF, clap_data_offset) = 0;
    }
}

CK_DLL_TICK(clap_tick)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);

    if(wrapper)
        *out = wrapper->tick(in);
    else
        *out = in;

    return TRUE;
}

CK_DLL_MFUN(clap_load)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    std::string path = GET_NEXT_STRING_SAFE(ARGS);

    RETURN->v_int = wrapper ? wrapper->load(path.c_str()) : 0;
}

CK_DLL_MFUN(clap_close)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    if(wrapper)
        wrapper->close();
}

CK_DLL_MFUN(clap_list)
{
    CLAPWrapper::listCLAPPlugins();
}

CK_DLL_MFUN(clap_set_param)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    t_CKINT index = GET_NEXT_INT(ARGS);
    t_CKFLOAT value = GET_NEXT_FLOAT(ARGS);

    if(wrapper)
        wrapper->setParameter(index, value);
}

CK_DLL_MFUN(clap_set_param_by_name)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    std::string name = GET_NEXT_STRING_SAFE(ARGS);
    t_CKFLOAT value = GET_NEXT_FLOAT(ARGS);

    RETURN->v_int = (wrapper && wrapper->setParameterByName(name.c_str(), value)) ? 1 : 0;
}

CK_DLL_MFUN(clap_get_param)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    t_CKINT index = GET_NEXT_INT(ARGS);

    RETURN->v_float = wrapper ? wrapper->getParameter(index) : 0.0;
}

CK_DLL_MFUN(clap_get_param_by_name)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    std::string name = GET_NEXT_STRING_SAFE(ARGS);

    RETURN->v_float = wrapper ? wrapper->getParameterByName(name.c_str()) : 0.0;
}

CK_DLL_MFUN(clap_get_param_name)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    t_CKINT index = GET_NEXT_INT(ARGS);

    const char* name = wrapper ? wrapper->getParameterName(index) : "";
    RETURN->v_string = API->object->create_string(VM, name, false);
}

CK_DLL_MFUN(clap_get_param_count)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    RETURN->v_int = wrapper ? wrapper->getParameterCount() : 0;
}

CK_DLL_MFUN(clap_bypass)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    t_CKINT bypass = GET_NEXT_INT(ARGS);

    if(wrapper)
        wrapper->setBypass(bypass != 0);
}

CK_DLL_MFUN(clap_send_midi)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    t_CKINT status = GET_NEXT_INT(ARGS);
    t_CKINT data1 = GET_NEXT_INT(ARGS);
    t_CKINT data2 = GET_NEXT_INT(ARGS);

    RETURN->v_int = (wrapper && wrapper->sendMIDI(status, data1, data2)) ? 1 : 0;
}

CK_DLL_MFUN(clap_note_on)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    t_CKINT pitch = GET_NEXT_INT(ARGS);
    t_CKINT velocity = GET_NEXT_INT(ARGS);

    RETURN->v_int = (wrapper && wrapper->noteOn(pitch, velocity)) ? 1 : 0;
}

CK_DLL_MFUN(clap_note_off)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    t_CKINT pitch = GET_NEXT_INT(ARGS);

    RETURN->v_int = (wrapper && wrapper->noteOff(pitch)) ? 1 : 0;
}

CK_DLL_MFUN(clap_is_instrument)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    RETURN->v_int = (wrapper && wrapper->isInstrument()) ? 1 : 0;
}

CK_DLL_MFUN(clap_get_preset_count)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    RETURN->v_int = wrapper ? wrapper->getPresetCount() : 0;
}

CK_DLL_MFUN(clap_get_preset_name)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    t_CKINT index = GET_NEXT_INT(ARGS);

    const char* name = wrapper ? wrapper->getPresetName(index) : "";
    RETURN->v_string = API->object->create_string(VM, name, false);
}

CK_DLL_MFUN(clap_load_preset)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    t_CKINT index = GET_NEXT_INT(ARGS);

    RETURN->v_int = (wrapper && wrapper->loadPreset(index)) ? 1 : 0;
}

CK_DLL_MFUN(clap_load_preset_by_name)
{
    CLAPWrapper* wrapper = (CLAPWrapper*)OBJ_MEMBER_INT(SELF, clap_data_offset);
    std::string name = GET_NEXT_STRING_SAFE(ARGS);

    RETURN->v_int = (wrapper && wrapper->loadPresetByName(name.c_str())) ? 1 : 0;
}
