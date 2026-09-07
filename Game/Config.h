#pragma once
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "../Models/Project_path.h"

class Config
{
  public:
    Config()  // Перегруженный оператор круглых скобок делает объект Config «вызываемым»
              // круглые скобки выступают в роли тригера для перезагрузки файла конфигурации и валидации параметров,
              // возвращению сылки на сам текущий активный экземпляр настроек или на корневой узел конфигурации. Ну или просто чтобы не вызывать каждый метод
    {
        reload();  // Перезагружает конфигурацию из JSON-файла для получения актуальных настроек
    }

    void reload()
    {
        std::ifstream fin(project_path + "settings.json");
        fin >> config;
        fin.close();
    }

    auto operator()(const string &setting_dir, const string &setting_name) const
    {
        return config[setting_dir][setting_name];
    }

  private:
    json config;
};
