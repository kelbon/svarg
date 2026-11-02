// ВХОД БЭКЕНДА: render2cxx обходит IR и пишет файлы схем, операций, сервера и клиента и .cpp спеки

#include "svarg/utils/svarg_assert.hpp"
#include "render_common.hpp"

namespace svarg::cxx {

// начало каждого сгенерированного заголовка: рантайм и file_prefix пользователя (его кодеки и т.п.)
std::string renderer::render_file_begin() const {
  std::string r = "#pragma once\n\n#include \"common_types.hpp\"\n";
  if (!options.file_prefix.empty())
    r += std::format("\n{}\n", options.file_prefix);
  return r + "\n";
}

std::string renderer::make_namespace(std::string_view spec, json_pointer jp) const {
  if (jp.segments_count() > 0)
    jp.rm_last_segment();
  std::string ns = make_cxx_ident(spec, options);
  std::string prefix;
  for (std::string_view seg : jp.segments()) {
    prefix += '/';
    prefix += seg;
    ns += "::";
    std::string unescaped = jp_unescape_segment(seg);
    ns += options.simple_cxx_identifiers ? to_cxx_ident_simple(unescaped) : to_cxx_ident(unescaped);
    // по этому пути лежит схема (тип), её подсхемы - в неймспейсе с суффиксом
    if (schema_locations.contains(std::pair(std::string(spec), prefix)))
      ns += options.inline_scope_suffix;
  }
  return ns;
}

std::string renderer::make_namespace(std::string_view spec, const json_pointer& jp,
                                     std::string_view relative_spec, const json_pointer& relative) const {
  // в "непростом" режиме имена типов полные: короткое имя могло бы оказаться скрыто членом структуры с тем
  // же именем (поле label типа label)
  if (!options.simple_cxx_identifiers)
    return std::format("::{}", make_namespace(spec, jp));
  return namespace_relative(make_namespace(relative_spec, relative), make_namespace(spec, jp));
}

std::vector<fs::path> renderer::render_all(fs::path output_dir, dd::generator<render_result> renders) const {
  std::error_code ec;
  // set для одинакового порядка
  std::set<fs::path> written;
  on_scope_failure(rm_written) {
    std::error_code ec;
    // пытаемся не менять исходную папку, но всё же удаляем файлы, которые были перезаписаны
    for (auto& p : written) {
      fs::path dir = p.parent_path();
      fs::remove(p, ec);
      while (!ec && !dir.empty() && fs::is_empty(dir, ec))
        fs::remove(std::exchange(dir, dir.parent_path()), ec);
      if (ec)
        emit_warn("error while removing directory {}, err: {}", p.generic_string(), ec.message());
    }
  };
  for (const auto& [filepath, rendered] : renders) {
    fs::path path = output_dir / filepath;
    auto [_, is_emplaced] = written.insert(path);
    if (!is_emplaced)
      throw render_error("two schemas rendered into same file `{}`", path.generic_string());
    fs::create_directories(path.parent_path(), ec);
    if (ec)
      throw render_error("filesystem error, message: {}, path: {}", ec.message(), path.generic_string());
    // заменяет файл если он уже есть
    write_file(path, rendered);
  }
  rm_written.no_longer_needed();
  return std::vector(written.begin(), written.end());
}

std::vector<fs::path> renderer::render_all_schemas(fs::path output_dir) const {
  auto foo = [r = this](this auto) -> dd::generator<render_result> {
    for (auto&& s : r->schemas()) {
      auto rr = r->render_schema(s);
      if (!rr.filename.empty())  // алиас, файл не нужен
        co_yield std::move(rr);
    }
  };
  if (!ir.schemas)
    return {};
  return render_all(std::move(output_dir), foo());
}

std::vector<fs::path> renderer::render_all_operations(fs::path output_dir, const std::set<ir::operation>& ops,
                                                      bool is_webhooks) const {
  auto foo = [&, r = this](this auto) -> dd::generator<render_result> {
    for (auto&& op : ops) {
      // workaround gcc ICE
      auto rr = r->render_operation(op, is_webhooks);
      co_yield std::move(rr);
    }
  };
  return render_all(std::move(output_dir), foo());
}

std::vector<fs::path> renderer::render2cxx(fs::path output_dir) const {
  if (ir.schemas && !ir.schemas->empty())
    render_all_schemas(output_dir);
  if (ir.operations && !ir.operations->empty())
    render_all_operations(output_dir, *ir.operations, /*is_webhooks=*/false);
  if (ir.webhooks && !ir.webhooks->empty())
    render_all_operations(output_dir, *ir.webhooks, /*is_webhooks=*/true);

  // all_schemas.hpp / all_operations.hpp / all_webhooks.hpp для каждой спеки (даже если схем нет,
  // генерируется пустой файл)
  // #pragma once нет, так как это файлы состоящие из #include со своими pragma once

  // имя спеки -> содержимое файла со всеми схемами
  struct all_files_content {
    std::string all_schemas;
    std::string all_operations;
    std::string all_webhooks;
  };
  std::map<std::string, all_files_content> spec2rendered;
  if (ir.schemas) {
    for (auto& s : *ir.schemas) {
      // include только именованные, так как все неименованные (инлайн схемы) через них тоже подключатся, т.к.
      // их кто-то использует. Именованные схемы никогда не алиасы, то есть имеют свой файл
      if (s.name)
        spec2rendered[s.spec].all_schemas +=
            std::format("#include \"{}\"\n", make_file_path(s.ref(), options));
    }
  }
  if (ir.operations) {
    for (auto& op : *ir.operations) {
      spec2rendered[op.spec].all_operations +=
          std::format("#include \"{}\"\n", make_file_path(op, options, /*is_webhook=*/false));
    }
  }
  if (ir.webhooks) {
    for (auto& op : *ir.webhooks) {
      spec2rendered[op.spec].all_webhooks +=
          std::format("#include \"{}\"\n", make_file_path(op, options, /*is_webhook=*/true));
    }
  }
  for (auto& [spec, all] : spec2rendered) {
    write_file(output_dir / spec / "all_schemas.hpp", all.all_schemas);
    write_file(output_dir / spec / "all_operations.hpp", all.all_operations);
    write_file(output_dir / spec / "all_webhooks.hpp", all.all_webhooks);
  }

  string_map<std::string> spec_sources;
  // server.hpp и client.hpp только если есть операции
  // один сервер на спеку (адрес сервера определяется listen), клиент на каждый набор серверов
  if (ir.operations) {
    string_map<std::vector<const ir::operation*>> spec2ops;
    for (auto& op : *ir.operations) {
      SVARG_ASSERT(op.has_path() || !op.servers);  // у вебхуков не должно быть серверов
      if (op.servers)
        spec2ops[op.spec].push_back(&op);
    }
    for (auto& [spec, ops] : spec2ops) {
      std::string server_source, client_source;
      write_file(output_dir / spec / "server.hpp", render_server(ops, spec, server_source));
      write_file(output_dir / spec / "client.hpp", render_client(ops, spec, client_source));
      spec_sources[spec] =
          std::format("#include \"{0}/server.hpp\"\n#include \"{0}/client.hpp\"\n\n{1}\n{2}\n", spec,
                      server_source, client_source);
    }
  }
  // <spec>.cpp - определения сервера и клиента. Явные инстанциации (де)сериализации операций - в нём же, если
  // часть одна, иначе поровну, подряд, в остальные options.sources_per_spec - 1 частей (см. render_options)
  if (options.sources_per_spec == 0)
    throw render_error("sources per spec must be at least 1");
  struct spec_operations {
    std::vector<std::string> includes, instantiations;
  };
  string_map<spec_operations> spec2ops;
  for (auto* ops : {&ir.operations, &ir.webhooks}) {
    if (!*ops)
      continue;
    for (auto& op : **ops) {
      spec_operations& so = spec2ops[op.spec];
      so.includes.push_back(
          std::format("#include \"{}\"\n", make_file_path(op, options, /*is_webhook=*/ops == &ir.webhooks)));
      so.instantiations.push_back(render_instantiations(op, /*is_extern=*/false));
    }
  }
  for (auto& [spec, so] : spec2ops) {
    std::string ident = make_cxx_ident(spec, options);
    // операции [begin, end) в одну часть
    auto part = [&](size_t begin, size_t end) {
      std::string includes, instantiations;
      for (size_t i = begin; i < end; ++i) {
        includes += so.includes[i];
        instantiations += so.instantiations[i];
      }
      return std::format("{}\n{}", includes, instantiations);
    };
    std::string& main_source = spec_sources[spec];
    if (options.sources_per_spec == 1) {
      main_source += part(0, so.includes.size());
      continue;
    }
    // все части пишутся, даже пустые: набор файлов зависит только от опции
    size_t n = options.sources_per_spec - 1, count = so.includes.size();
    for (size_t i = 0; i < n; ++i)
      write_file(output_dir / spec / std::format("{}_{}.cpp", ident, i + 1), part(i * count / n, (i + 1) * count / n));
  }
  for (auto& [spec, src] : spec_sources)
    write_file(output_dir / spec / std::format("{}.cpp", make_cxx_ident(spec, options)), src);
  remove_duplicates(generated);
  std::stable_sort(generated.begin(), generated.end(),
                   [](const fs::path& a, const fs::path& b) { return a.extension() < b.extension(); });
  return std::move(generated);
}

}  // namespace svarg::cxx
