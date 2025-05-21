#include "RegistryPanel.hpp"

struct RegistryPanel::Data
{
    Data(RegistryPanel* owner);
    RegistryPanel* owner;
};

RegistryPanel::Data::Data(RegistryPanel* owner)
{
    this->owner = owner;
}

RegistryPanel::RegistryPanel(wxWindow* parent) : wxPanel(parent)
{
    m_data = new Data(this);
}

RegistryPanel::~RegistryPanel()
{
    delete m_data;
}
