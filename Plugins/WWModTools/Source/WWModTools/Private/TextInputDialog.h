#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SEditableTextBox.h"

// Minimal modal single-line text prompt.
class STextInputDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STextInputDialog) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(FText, InitialText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Returns false if cancelled or left empty.
	static bool ShowModal(const FText& Title, const FText& Label, const FString& Initial, FString& OutText);

private:
	FReply OnOKClicked();
	FReply OnCancelClicked();
	bool IsOKEnabled() const;

	TSharedPtr<SEditableTextBox> TextBox;
	TWeakPtr<SWindow>            ParentWindow;
	FString                      Result;
	bool                         bConfirmed = false;
};
