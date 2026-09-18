#include "NewModDialog.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"

void SNewModDialog::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBox).MinDesiredWidth(360.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 8.f, 8.f, 4.f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Mod name (letters and digits only, no spaces):")))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f, 8.f, 8.f)
			[
				SAssignNew(NameBox, SEditableTextBox)
				.HintText(FText::FromString(TEXT("e.g. MyAwesomeMod")))
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Type)
				{
					if (Type == ETextCommit::OnEnter && IsOKEnabled())
					{
						OnOKClicked();
					}
				})
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeField(TEXT("Display name (optional, defaults to mod name):"),
					TEXT("e.g. My Awesome Mod"), DisplayNameBox)
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeField(TEXT("Description:"), TEXT("e.g. Makes the stone path look prettier!"), DescriptionBox)
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeField(TEXT("Version:"), TEXT("1.0"), VersionBox)
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeField(TEXT("Created by:"), TEXT("Your name"), CreatedByBox)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f, 8.f, 8.f)
			[
				SNew(SUniformGridPanel).SlotPadding(FMargin(4.f, 0.f))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Create")))
					.IsEnabled_Raw(this, &SNewModDialog::IsOKEnabled)
					.OnClicked_Raw(this, &SNewModDialog::OnOKClicked)
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Cancel")))
					.OnClicked_Raw(this, &SNewModDialog::OnCancelClicked)
				]
			]
		]
	];
}

TSharedRef<SWidget> SNewModDialog::MakeField(
	const TCHAR* Label, const TCHAR* Hint, TSharedPtr<SEditableTextBox>& OutBox)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f, 8.f, 4.f)
		[
			SNew(STextBlock).Text(FText::FromString(Label))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f, 8.f, 8.f)
		[
			SAssignNew(OutBox, SEditableTextBox)
			.HintText(FText::FromString(Hint))
		];
}

bool SNewModDialog::ShowModal(FModInfo& OutInfo)
{
	TSharedRef<SNewModDialog> Content = SNew(SNewModDialog);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("New Mod")))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			Content
		];

	Content->ParentWindow = Window;

	GEditor->EditorAddModalWindow(Window);

	if (!Content->bConfirmed) return false;
	OutInfo = Content->Result;
	return true;
}

FReply SNewModDialog::OnOKClicked()
{
	auto Get = [](const TSharedPtr<SEditableTextBox>& Box)
	{
		return Box->GetText().ToString().TrimStartAndEnd();
	};
	Result.FolderName  = Get(NameBox);
	Result.DisplayName = Get(DisplayNameBox);
	Result.Description = Get(DescriptionBox);
	Result.CreatedBy   = Get(CreatedByBox);
	const FString Version = Get(VersionBox);
	if (!Version.IsEmpty()) Result.Version = Version;
	bConfirmed = true;
	if (ParentWindow.IsValid())
	{
		ParentWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SNewModDialog::OnCancelClicked()
{
	if (ParentWindow.IsValid())
	{
		ParentWindow.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

bool SNewModDialog::IsOKEnabled() const
{
	if (!NameBox.IsValid()) return false;
	const FString Text = NameBox->GetText().ToString().TrimStartAndEnd();
	if (Text.IsEmpty()) return false;
	for (TCHAR Ch : Text)
	{
		if (!FChar::IsAlnum(Ch) && Ch != TEXT('_')) return false;
	}
	return true;
}
