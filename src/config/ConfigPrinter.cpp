#include "Config.hpp"

void ConfigPrinter::print(std::ostream& out, const Config& config) {
    out << "Configuration loaded successfully:" << NEWLINE;
    
    printServerInfo(out, config);
    out << NEWLINE;
    
    printErrorPages(out, config);
    out << NEWLINE;
    
    printLocations(out, config);
}

void ConfigPrinter::printServerInfo(std::ostream& out, const Config& config) {
    out << "Port: " << config.getPort() << NEWLINE
        << "Server name: " << config.getServerName() << NEWLINE
        << "Root: " << config.getRoot() << NEWLINE
        << "Index: " << config.getIndex() << NEWLINE
        << "Client max body size: " << config.getClientMaxBodySize() << " bytes" << NEWLINE
        << "Number of locations: " << config.getLocations().size();
}

void ConfigPrinter::printErrorPages(std::ostream& out, const Config& config) {
    out << "Error pages:";
    const auto& error_pages = config.getErrorPages();
    if (error_pages.empty()) {
        out << " none" << NEWLINE;
        return;
    }
    out << NEWLINE;
    
    for (const auto& [code, path] : error_pages) {
        out << INDENT << code << " -> " << path << NEWLINE;
    }
}

void ConfigPrinter::printLocations(std::ostream& out, const Config& config) {
    out << "Locations:" << NEWLINE;
    
    for (const auto& location : config.getLocations()) {
        if (!location) continue;  // Skip null locations if any
        out << NEWLINE;
        printLocation(out, *location);
    }
}

void ConfigPrinter::printLocation(std::ostream& out, const Location& location) {
    out << "Path: " << location.getPath() << NEWLINE;
    
    if (!location.getRoot().empty()) {
        out << INDENT << "Root: " << location.getRoot() << NEWLINE;
    }
    
    if (!location.getIndex().empty()) {
        out << INDENT << "Index: " << location.getIndex() << NEWLINE;
    }
    
    out << INDENT << "Autoindex: " << (location.getAutoindex() ? "on" : "off") << NEWLINE;
    
    printMethods(out, location.getAllowedMethods());
    
    if (location.hasReturn()) {
        printReturnDirective(out, location.getReturn());
    }
}

void ConfigPrinter::printReturnDirective(std::ostream& out, const Location::ReturnDirective& ret) {
    out << INDENT << "Return: ";
    
    if (ret.type == Location::ReturnType::REDIRECT) {
        out << ret.code << " -> " << ret.body << " (Redirect)";
    } else {
        out << ret.code;
        if (!ret.body.empty()) {
            out << " \"" << ret.body << "\"";
        }
        out << " (Response)";
    }
    out << NEWLINE;
}

void ConfigPrinter::printMethods(std::ostream& out, const std::vector<std::string>& methods) {
    if (!methods.empty()) {
        out << INDENT << "Methods:";
        for (const auto& method : methods) {
            out << " " << method;
        }
        out << NEWLINE;
    }
}